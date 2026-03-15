#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define MAX_COMMAND_LENGTH 256
#define MAX_ARGS 128

static const char *k_builtin_completion_candidates[] = {"echo", "exit"};

// Treat only space and tab as argument separators in this stage.
static int is_inline_whitespace(char c) { return c == ' ' || c == '\t'; }

static int starts_with(const char *text, const char *prefix,
                       size_t prefix_len) {
  return strncmp(text, prefix, prefix_len) == 0;
}

// In double quotes, only these two escape targets are handled for now.
static int is_escapable_in_double_quotes(char c) {
  return c == '"' || c == '\\';
}

// Parsed redirection intent for one command line.
typedef struct {
  char *stdout_file;
  int stdout_append;
  char *stderr_file;
  int stderr_append;
} RedirectionSpec;

// Saved original descriptors so they can be restored after command execution.
typedef struct {
  int saved_stdout;
  int saved_stderr;
} SavedDescriptors;

typedef struct {
  int enabled;
  struct termios original;
} TerminalMode;

typedef struct {
  int pending_list;
  int pending_first_token;
  char prefix[MAX_COMMAND_LENGTH];
} TabCompletionState;

typedef struct {
  char *text;
  int is_directory;
} FileCompletionEntry;

static void restore_fd(int saved_fd, int target_fd);
static size_t common_prefix_length(const char *a, const char *b);

static int compare_strings(const void *a, const void *b) {
  const char *const *sa = (const char *const *)a;
  const char *const *sb = (const char *const *)b;
  return strcmp(*sa, *sb);
}

static int compare_file_completion_entries(const void *a, const void *b) {
  const FileCompletionEntry *ea = (const FileCompletionEntry *)a;
  const FileCompletionEntry *eb = (const FileCompletionEntry *)b;
  return strcmp(ea->text, eb->text);
}

static size_t
longest_common_prefix_length_for_entries(FileCompletionEntry *entries,
                                         size_t entry_count) {
  if (entry_count == 0) {
    return 0;
  }

  size_t lcp = strlen(entries[0].text);
  for (size_t i = 1; i < entry_count; i++) {
    size_t current = common_prefix_length(entries[0].text, entries[i].text);
    if (current < lcp) {
      lcp = current;
    }
  }
  return lcp;
}

static void reset_tab_completion_state(TabCompletionState *state) {
  state->pending_list = 0;
  state->pending_first_token = 0;
  state->prefix[0] = '\0';
}

static char *duplicate_path_env(void) {
  const char *path_env = getenv("PATH");
  if (path_env == NULL || *path_env == '\0') {
    return NULL;
  }

  size_t path_len = strlen(path_env);
  char *path_copy = malloc(path_len + 1);
  if (path_copy == NULL) {
    return NULL;
  }
  memcpy(path_copy, path_env, path_len + 1);
  return path_copy;
}

static int append_unique_match(char ***items, size_t *count, size_t *capacity,
                               const char *name) {
  for (size_t i = 0; i < *count; i++) {
    if (strcmp((*items)[i], name) == 0) {
      return 0;
    }
  }

  if (*count == *capacity) {
    size_t new_capacity = (*capacity == 0) ? 16 : (*capacity * 2);
    char **new_items = realloc(*items, new_capacity * sizeof(char *));
    if (new_items == NULL) {
      return -1;
    }
    *items = new_items;
    *capacity = new_capacity;
  }

  size_t len = strlen(name);
  char *copy = malloc(len + 1);
  if (copy == NULL) {
    return -1;
  }
  memcpy(copy, name, len + 1);

  (*items)[(*count)++] = copy;
  return 0;
}

static void free_matches(char **items, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(items[i]);
  }
  free(items);
}

static void free_file_completion_entries(FileCompletionEntry *entries,
                                         size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(entries[i].text);
  }
  free(entries);
}

static int append_completion_span(char *line, size_t *len, size_t line_size,
                                  const char *text, size_t from,
                                  size_t to_exclusive) {
  if (to_exclusive < from) {
    return 0;
  }

  size_t add_len = to_exclusive - from;
  if (*len + add_len >= line_size) {
    return 0;
  }

  for (size_t i = from; i < to_exclusive; i++) {
    line[(*len)++] = text[i];
    putchar(text[i]);
  }
  line[*len] = '\0';
  return 1;
}

static int append_completion_char(char *line, size_t *len, size_t line_size,
                                  char ch) {
  if (*len + 1 >= line_size) {
    return 0;
  }

  line[(*len)++] = ch;
  line[*len] = '\0';
  putchar(ch);
  return 1;
}

static void print_command_match_list(char **matches, size_t match_count,
                                     const char *line) {
  putchar('\n');
  for (size_t i = 0; i < match_count; i++) {
    if (i > 0) {
      printf("  ");
    }
    printf("%s", matches[i]);
  }
  printf("\n$ %s", line);
}

static void print_file_match_list(FileCompletionEntry *entries,
                                  size_t entry_count, const char *line) {
  putchar('\n');
  for (size_t i = 0; i < entry_count; i++) {
    if (i > 0) {
      printf("  ");
    }
    printf("%s", entries[i].text);
    if (entries[i].is_directory) {
      putchar('/');
    }
  }
  printf("\n$ %s", line);
}

static void set_pending_completion(TabCompletionState *state,
                                   const char *prefix, size_t prefix_len,
                                   int first_token) {
  memcpy(state->prefix, prefix, prefix_len + 1);
  state->pending_list = 1;
  state->pending_first_token = first_token;
}

static size_t common_prefix_length(const char *a, const char *b) {
  size_t i = 0;
  while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
    i++;
  }
  return i;
}

static size_t longest_common_prefix_length(char **matches, size_t match_count) {
  if (match_count == 0) {
    return 0;
  }

  size_t lcp = strlen(matches[0]);
  for (size_t i = 1; i < match_count; i++) {
    size_t current = common_prefix_length(matches[0], matches[i]);
    if (current < lcp) {
      lcp = current;
    }
  }
  return lcp;
}

static int collect_external_completion_matches(const char *prefix,
                                               size_t prefix_len,
                                               char ***matches,
                                               size_t *match_count,
                                               size_t *capacity) {
  char *path_copy = duplicate_path_env();
  if (path_copy == NULL) {
    return 0;
  }

  for (char *dir = strtok(path_copy, ":"); dir != NULL;
       dir = strtok(NULL, ":")) {
    DIR *dp = opendir(dir);
    if (dp == NULL) {
      continue;
    }

    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
      const char *name = entry->d_name;
      if (!starts_with(name, prefix, prefix_len)) {
        continue;
      }

      char full_path[PATH_MAX];
      if (snprintf(full_path, sizeof(full_path), "%s/%s", dir, name) >=
          (int)sizeof(full_path)) {
        continue;
      }

      struct stat st;
      if (stat(full_path, &st) != 0 || !S_ISREG(st.st_mode) ||
          access(full_path, X_OK) != 0) {
        continue;
      }

      if (append_unique_match(matches, match_count, capacity, name) != 0) {
        closedir(dp);
        free(path_copy);
        return -1;
      }
    }

    closedir(dp);
  }

  free(path_copy);
  return 0;
}

static int collect_completion_matches(const char *prefix, size_t prefix_len,
                                      char ***matches, size_t *match_count) {
  *matches = NULL;
  *match_count = 0;
  size_t capacity = 0;

  for (size_t i = 0; i < sizeof(k_builtin_completion_candidates) /
                             sizeof(k_builtin_completion_candidates[0]);
       i++) {
    const char *name = k_builtin_completion_candidates[i];
    if (!starts_with(name, prefix, prefix_len)) {
      continue;
    }
    if (append_unique_match(matches, match_count, &capacity, name) != 0) {
      free_matches(*matches, *match_count);
      *matches = NULL;
      *match_count = 0;
      return -1;
    }
  }

  if (collect_external_completion_matches(prefix, prefix_len, matches,
                                          match_count, &capacity) != 0) {
    free_matches(*matches, *match_count);
    *matches = NULL;
    *match_count = 0;
    return -1;
  }

  if (*match_count > 1) {
    qsort(*matches, *match_count, sizeof(char *), compare_strings);
  }

  return 0;
}

static int collect_file_completion_entries(const char *prefix,
                                           size_t prefix_len,
                                           FileCompletionEntry **entries,
                                           size_t *entry_count) {
  char directory[MAX_COMMAND_LENGTH];
  char output_prefix[MAX_COMMAND_LENGTH];
  const char *name_prefix = prefix;
  size_t name_prefix_len = prefix_len;

  *entries = NULL;
  *entry_count = 0;
  size_t capacity = 0;

  directory[0] = '.';
  directory[1] = '\0';
  output_prefix[0] = '\0';

  const char *last_slash = strrchr(prefix, '/');
  if (last_slash != NULL) {
    size_t dir_len = (size_t)(last_slash - prefix + 1);
    if (dir_len >= sizeof(directory) || dir_len >= sizeof(output_prefix)) {
      return 0;
    }

    memcpy(directory, prefix, dir_len);
    directory[dir_len] = '\0';
    memcpy(output_prefix, prefix, dir_len);
    output_prefix[dir_len] = '\0';

    name_prefix = last_slash + 1;
    name_prefix_len = prefix_len - dir_len;
  }

  DIR *dp = opendir(directory);
  if (dp == NULL) {
    return 0;
  }

  struct dirent *entry;
  while ((entry = readdir(dp)) != NULL) {
    const char *name = entry->d_name;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
      continue;
    }
    if (!starts_with(name, name_prefix, name_prefix_len)) {
      continue;
    }

    char candidate[MAX_COMMAND_LENGTH];
    if (snprintf(candidate, sizeof(candidate), "%s%s", output_prefix, name) >=
        (int)sizeof(candidate)) {
      continue;
    }

    if (*entry_count == capacity) {
      size_t new_capacity = (capacity == 0) ? 8 : (capacity * 2);
      FileCompletionEntry *new_entries =
          realloc(*entries, new_capacity * sizeof(FileCompletionEntry));
      if (new_entries == NULL) {
        closedir(dp);
        free_file_completion_entries(*entries, *entry_count);
        *entries = NULL;
        *entry_count = 0;
        return -1;
      }
      *entries = new_entries;
      capacity = new_capacity;
    }

    size_t len = strlen(candidate);
    char *copy = malloc(len + 1);
    if (copy == NULL) {
      closedir(dp);
      free_file_completion_entries(*entries, *entry_count);
      *entries = NULL;
      *entry_count = 0;
      return -1;
    }

    memcpy(copy, candidate, len + 1);
    (*entries)[*entry_count].text = copy;

    struct stat st;
    (*entries)[*entry_count].is_directory =
        (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
    (*entry_count)++;
  }

  closedir(dp);
  if (*entry_count > 1) {
    qsort(*entries, *entry_count, sizeof(FileCompletionEntry),
          compare_file_completion_entries);
  }
  return 0;
}

static int is_first_token_position(const char *line, size_t word_start) {
  for (size_t i = 0; i < word_start; i++) {
    if (!is_inline_whitespace(line[i])) {
      return 0;
    }
  }
  return 1;
}

static int enable_interactive_input_mode(TerminalMode *mode) {
  mode->enabled = 0;

  if (!isatty(STDIN_FILENO)) {
    return 0;
  }

  if (tcgetattr(STDIN_FILENO, &mode->original) != 0) {
    return -1;
  }

  struct termios raw = mode->original;
  raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
    return -1;
  }

  mode->enabled = 1;
  return 0;
}

static void restore_interactive_input_mode(TerminalMode *mode) {
  if (!mode->enabled) {
    return;
  }
  tcsetattr(STDIN_FILENO, TCSANOW, &mode->original);
  mode->enabled = 0;
}

// Try to autocomplete the current token when TAB is pressed.
static void autocomplete_command_live(char *line, size_t *len, size_t line_size,
                                      TabCompletionState *state) {
  // Complete the token currently being typed (the substring after the last
  // whitespace before the cursor).
  size_t word_start = *len;
  while (word_start > 0 && !is_inline_whitespace(line[word_start - 1])) {
    word_start--;
  }

  int first_token = is_first_token_position(line, word_start);
  if (word_start == *len && first_token) {
    reset_tab_completion_state(state);
    return;
  }

  size_t partial_len = *len - word_start;
  if (partial_len + 1 > sizeof(state->prefix)) {
    reset_tab_completion_state(state);
    return;
  }

  char current_prefix[MAX_COMMAND_LENGTH];
  memcpy(current_prefix, line + word_start, partial_len);
  current_prefix[partial_len] = '\0';

  // Argument position: complete from current directory when there is exactly
  // one file/path match.
  if (!first_token) {
    FileCompletionEntry *entries = NULL;
    size_t entry_count = 0;
    if (collect_file_completion_entries(current_prefix, partial_len, &entries,
                                        &entry_count) != 0) {
      putchar('\a');
      reset_tab_completion_state(state);
      return;
    }

    if (entry_count == 0) {
      putchar('\a');
      reset_tab_completion_state(state);
      free_file_completion_entries(entries, entry_count);
      return;
    }

    if (entry_count > 1) {
      size_t lcp_len =
          longest_common_prefix_length_for_entries(entries, entry_count);
      if (lcp_len > partial_len) {
        if (append_completion_span(line, len, line_size, entries[0].text,
                                   partial_len, lcp_len)) {
          free_file_completion_entries(entries, entry_count);
          reset_tab_completion_state(state);
          return;
        }
      }

      if (state->pending_list && !state->pending_first_token &&
          strcmp(state->prefix, current_prefix) == 0) {
        print_file_match_list(entries, entry_count, line);
        reset_tab_completion_state(state);
      } else {
        set_pending_completion(state, current_prefix, partial_len, 0);
        putchar('\a');
      }

      free_file_completion_entries(entries, entry_count);
      return;
    }

    const char *matched_filename = entries[0].text;
    int matched_is_directory = entries[0].is_directory;

    size_t matched_len = strlen(matched_filename);
    if (!append_completion_span(line, len, line_size, matched_filename,
                                partial_len, matched_len)) {
      reset_tab_completion_state(state);
      free_file_completion_entries(entries, entry_count);
      return;
    }

    if (matched_is_directory) {
      if (!append_completion_char(line, len, line_size, '/')) {
        reset_tab_completion_state(state);
        free_file_completion_entries(entries, entry_count);
        return;
      }
    } else {
      if (!append_completion_char(line, len, line_size, ' ')) {
        reset_tab_completion_state(state);
        free_file_completion_entries(entries, entry_count);
        return;
      }
    }

    reset_tab_completion_state(state);
    free_file_completion_entries(entries, entry_count);
    return;
  }

  char **matches = NULL;
  size_t match_count = 0;
  if (collect_completion_matches(current_prefix, partial_len, &matches,
                                 &match_count) != 0) {
    reset_tab_completion_state(state);
    putchar('\a');
    return;
  }

  if (match_count == 0) {
    free_matches(matches, match_count);
    reset_tab_completion_state(state);
    putchar('\a');
    return;
  }

  if (match_count > 1) {
    size_t lcp_len = longest_common_prefix_length(matches, match_count);
    if (lcp_len > partial_len) {
      if (append_completion_span(line, len, line_size, matches[0], partial_len,
                                 lcp_len)) {
        free_matches(matches, match_count);
        reset_tab_completion_state(state);
        return;
      }
    }

    if (state->pending_list && strcmp(state->prefix, current_prefix) == 0) {
      print_command_match_list(matches, match_count, line);
      reset_tab_completion_state(state);
    } else {
      set_pending_completion(state, current_prefix, partial_len, 1);
      putchar('\a');
    }

    free_matches(matches, match_count);
    return;
  }

  const char *matched = matches[0];
  size_t matched_len = strlen(matched);
  if (!append_completion_span(line, len, line_size, matched, partial_len,
                              matched_len) ||
      !append_completion_char(line, len, line_size, ' ')) {
    free_matches(matches, match_count);
    reset_tab_completion_state(state);
    return;
  }

  free_matches(matches, match_count);
  reset_tab_completion_state(state);
}

// Read one command line from stdin in a key-by-key fashion so TAB completion
// can happen immediately after the key press.
static int read_command_line(char *line, size_t line_size) {
  size_t len = 0;
  TabCompletionState tab_state;
  reset_tab_completion_state(&tab_state);
  line[0] = '\0';

  while (1) {
    char ch = '\0';
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n <= 0) {
      return -1;
    }

    if (ch == '\n' || ch == '\r') {
      putchar('\n');
      line[len] = '\0';
      return (int)len;
    }

    if (ch == '\t') {
      autocomplete_command_live(line, &len, line_size, &tab_state);
      continue;
    }

    // Basic backspace support for interactive editing.
    if (ch == 127 || ch == '\b') {
      if (len > 0) {
        len--;
        line[len] = '\0';
        printf("\b \b");
      }
      reset_tab_completion_state(&tab_state);
      continue;
    }

    if (len + 1 >= line_size) {
      continue;
    }

    line[len++] = ch;
    line[len] = '\0';
    putchar(ch);
    reset_tab_completion_state(&tab_state);
  }
}

// Builtins currently supported by this shell.
static int is_builtin(const char *cmd) {
  return strcmp(cmd, "echo") == 0 || strcmp(cmd, "type") == 0 ||
         strcmp(cmd, "exit") == 0 || strcmp(cmd, "pwd") == 0 ||
         strcmp(cmd, "cd") == 0;
}

// Search PATH for an executable file and return its absolute path.
static int find_executable_in_path(const char *name, char *out_path,
                                   size_t out_path_size) {
  if (name == NULL || *name == '\0') {
    return 0;
  }

  char *path_copy = duplicate_path_env();
  if (path_copy == NULL) {
    return 0;
  }

  for (char *dir = strtok(path_copy, ":"); dir != NULL;
       dir = strtok(NULL, ":")) {
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, name);

    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode) &&
        access(full_path, X_OK) == 0) {
      snprintf(out_path, out_path_size, "%s", full_path);
      free(path_copy);
      return 1;
    }
  }

  free(path_copy);
  return 0;
}

// Implementation of the "type" builtin.
static void handle_type(const char *name) {
  if (name == NULL || *name == '\0') {
    printf("type: missing operand\r\n");
    return;
  }

  // 1) builtin
  if (is_builtin(name)) {
    printf("%s is a shell builtin\r\n", name);
    return;
  }

  char full_path[PATH_MAX];
  if (find_executable_in_path(name, full_path, sizeof(full_path))) {
    printf("%s is %s\r\n", name, full_path);
    return;
  }

  printf("%s: not found\r\n", name);
}

// Implementation of the "cd" builtin (supports ~ via HOME).
static void handle_cd(const char *path) {
  if (path == NULL || *path == '\0') {
    return;
  }

  const char *target = path;
  if (strcmp(path, "~") == 0) {
    const char *home = getenv("HOME");
    if (home == NULL || *home == '\0') {
      return;
    }
    target = home;
  }

  if (chdir(target) != 0) {
    printf("cd: %s: No such file or directory\r\n", path);
  }
}

// Parse command line into argv-like tokens.
// Rules implemented here:
// - Whitespace separates arguments when outside quotes.
// - Single and double quotes remove delimiter meaning.
// - Adjacent quoted/unquoted segments are concatenated.
// - Backslash escaping is supported per current stage requirements.
static int parse_arguments(char *line, char **args, int max_args) {
  char *read = line;
  char *write = line;
  int arg_count = 0;

  while (*read != '\0') {
    while (is_inline_whitespace(*read)) {
      read++;
    }

    if (*read == '\0') {
      break;
    }

    if (arg_count >= max_args - 1) {
      break;
    }

    args[arg_count++] = write;
    int in_single_quote = 0;
    int in_double_quote = 0;

    // Parse one shell word. Quote characters are removed and adjacent quoted
    // segments are concatenated into the same argument.
    while (*read != '\0') {
      if (in_double_quote && *read == '\\') {
        char next = *(read + 1);
        // In double quotes, only \" and \\ are special at this stage.
        if (is_escapable_in_double_quotes(next)) {
          read++;
          *write++ = *read++;
          continue;
        }
      }

      if (!in_single_quote && !in_double_quote && *read == '\\') {
        // Outside quotes, backslash escapes the next character literally.
        read++;
        if (*read != '\0') {
          *write++ = *read++;
        }
        continue;
      }

      if (*read == '\'' && !in_double_quote) {
        in_single_quote = !in_single_quote;
        read++;
        continue;
      }

      if (*read == '"' && !in_single_quote) {
        in_double_quote = !in_double_quote;
        read++;
        continue;
      }

      if (!in_single_quote && !in_double_quote && is_inline_whitespace(*read)) {
        while (is_inline_whitespace(*read)) {
          read++;
        }
        break;
      }

      *write++ = *read++;
    }

    *write++ = '\0';
  }

  args[arg_count] = NULL;
  return arg_count;
}

// Split parsed tokens into:
// 1) command arguments that should be passed to exec/builtin
// 2) redirection targets/modes for stdout and stderr
//
// Supported redirection forms:
// - stdout overwrite: > file, 1> file, >file, 1>file
// - stdout append:    >> file, 1>> file, >>file, 1>>file
// - stderr overwrite: 2> file, 2>file
// - stderr append:    2>> file, 2>>file

static void assign_redirection(RedirectionSpec *redir, int fd, char *target,
                               int append) {
  if (fd == STDERR_FILENO) {
    redir->stderr_file = target;
    redir->stderr_append = append;
    return;
  }

  redir->stdout_file = target;
  redir->stdout_append = append;
}

// Parse tokens where target path is attached to the operator, e.g. 1>>file.
static int parse_inline_redirection_token(char *token, int *fd, int *append,
                                          char **target) {
  if (strncmp(token, "1>>", 3) == 0 && token[3] != '\0') {
    *fd = STDOUT_FILENO;
    *append = 1;
    *target = token + 3;
    return 1;
  }

  if (strncmp(token, "2>>", 3) == 0 && token[3] != '\0') {
    *fd = STDERR_FILENO;
    *append = 1;
    *target = token + 3;
    return 1;
  }

  if (strncmp(token, ">>", 2) == 0 && token[2] != '\0') {
    *fd = STDOUT_FILENO;
    *append = 1;
    *target = token + 2;
    return 1;
  }

  if (strncmp(token, "1>", 2) == 0 && token[2] != '\0' && token[2] != '>') {
    *fd = STDOUT_FILENO;
    *append = 0;
    *target = token + 2;
    return 1;
  }

  if (strncmp(token, "2>", 2) == 0 && token[2] != '\0' && token[2] != '>') {
    *fd = STDERR_FILENO;
    *append = 0;
    *target = token + 2;
    return 1;
  }

  if (token[0] == '>' && token[1] != '\0' && token[1] != '>') {
    *fd = STDOUT_FILENO;
    *append = 0;
    *target = token + 1;
    return 1;
  }

  return 0;
}

// Parse tokens where target path is provided in the next argument, e.g. 2>
// file.
static int parse_separate_redirection_token(const char *token, int *fd,
                                            int *append) {
  if (strcmp(token, ">>") == 0 || strcmp(token, "1>>") == 0) {
    *fd = STDOUT_FILENO;
    *append = 1;
    return 1;
  }

  if (strcmp(token, "2>>") == 0) {
    *fd = STDERR_FILENO;
    *append = 1;
    return 1;
  }

  if (strcmp(token, ">") == 0 || strcmp(token, "1>") == 0) {
    *fd = STDOUT_FILENO;
    *append = 0;
    return 1;
  }

  if (strcmp(token, "2>") == 0) {
    *fd = STDERR_FILENO;
    *append = 0;
    return 1;
  }

  return 0;
}

static int split_command_and_redirections(char **args, int arg_count,
                                          char **command_args, int max_args,
                                          RedirectionSpec *redir) {
  int command_arg_count = 0;
  redir->stdout_file = NULL;
  redir->stdout_append = 0;
  redir->stderr_file = NULL;
  redir->stderr_append = 0;

  for (int i = 0; i < arg_count; i++) {
    char *token = args[i];
    int fd = -1;
    int append = 0;
    char *inline_target = NULL;

    if (parse_inline_redirection_token(token, &fd, &append, &inline_target)) {
      assign_redirection(redir, fd, inline_target, append);
      continue;
    }

    if (parse_separate_redirection_token(token, &fd, &append)) {
      if (i + 1 >= arg_count) {
        return -1;
      }
      assign_redirection(redir, fd, args[++i], append);
      continue;
    }

    if (command_arg_count >= max_args - 1) {
      break;
    }
    command_args[command_arg_count++] = token;
  }

  command_args[command_arg_count] = NULL;
  return command_arg_count;
}

// Redirect one descriptor to file and return a saved copy of the original fd.
static int redirect_fd_to_file(int fd_to_redirect, const char *path,
                               int append) {
  int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
  int fd = open(path, flags, 0644);
  if (fd < 0) {
    return -1;
  }

  int saved_fd = dup(fd_to_redirect);
  if (saved_fd < 0) {
    close(fd);
    return -1;
  }

  if (dup2(fd, fd_to_redirect) < 0) {
    close(fd);
    close(saved_fd);
    return -1;
  }

  close(fd);
  return saved_fd;
}

// Apply parsed redirections before command execution.
// On failure, any already-applied redirection is rolled back.
static int apply_redirections(const RedirectionSpec *redir,
                              SavedDescriptors *saved) {
  saved->saved_stdout = -1;
  saved->saved_stderr = -1;

  if (redir->stdout_file != NULL) {
    saved->saved_stdout = redirect_fd_to_file(STDOUT_FILENO, redir->stdout_file,
                                              redir->stdout_append);
    if (saved->saved_stdout < 0) {
      return -1;
    }
  }

  if (redir->stderr_file != NULL) {
    saved->saved_stderr = redirect_fd_to_file(STDERR_FILENO, redir->stderr_file,
                                              redir->stderr_append);
    if (saved->saved_stderr < 0) {
      if (saved->saved_stdout >= 0) {
        restore_fd(saved->saved_stdout, STDOUT_FILENO);
        saved->saved_stdout = -1;
      }
      return -1;
    }
  }

  return 0;
}

// Restore descriptors after the command finishes.
static void restore_redirections(SavedDescriptors *saved) {
  if (saved->saved_stderr >= 0) {
    restore_fd(saved->saved_stderr, STDERR_FILENO);
    saved->saved_stderr = -1;
  }

  if (saved->saved_stdout >= 0) {
    restore_fd(saved->saved_stdout, STDOUT_FILENO);
    saved->saved_stdout = -1;
  }
}

// Restore one descriptor from a saved copy.
static void restore_fd(int saved_fd, int target_fd) {
  if (target_fd == STDOUT_FILENO) {
    fflush(stdout);
  } else if (target_fd == STDERR_FILENO) {
    fflush(stderr);
  }

  dup2(saved_fd, target_fd);
  close(saved_fd);
}

// Implementation of the "echo" builtin.
static void handle_echo(char **args, int arg_count) {
  for (int i = 1; i < arg_count; i++) {
    if (i > 1) {
      printf(" ");
    }
    printf("%s", args[i]);
  }
  printf("\r\n");
}

// Implementation of the "pwd" builtin.
static void handle_pwd(void) {
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("%s\r\n", cwd);
  } else {
    perror("pwd");
  }
}

// Execute a non-builtin command by searching PATH and using fork/exec.
static void execute_external_command(char **args) {
  char full_path[PATH_MAX];
  if (!find_executable_in_path(args[0], full_path, sizeof(full_path))) {
    printf("%s: command not found\r\n", args[0]);
    return;
  }

  pid_t pid = fork();
  if (pid == 0) {
    execv(full_path, args);
    exit(1);
  }

  if (pid > 0) {
    waitpid(pid, NULL, 0);
  }
}

// Dispatch one parsed command to builtin/external handlers.
// Return 1 if shell should exit, otherwise 0.
static int execute_command(char **args, int arg_count) {
  char *cmd = args[0];

  if (strcmp(cmd, "exit") == 0) {
    return 1;
  }

  if (strcmp(cmd, "echo") == 0) {
    handle_echo(args, arg_count);
    return 0;
  }

  if (strcmp(cmd, "pwd") == 0) {
    handle_pwd();
    return 0;
  }

  if (strcmp(cmd, "cd") == 0) {
    handle_cd(arg_count > 1 ? args[1] : NULL);
    return 0;
  }

  if (strcmp(cmd, "type") == 0) {
    handle_type(arg_count > 1 ? args[1] : NULL);
    return 0;
  }

  execute_external_command(args);
  return 0;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  TerminalMode terminal_mode;
  if (enable_interactive_input_mode(&terminal_mode) != 0) {
    terminal_mode.enabled = 0;
  }

  // Flush after every printf
  setbuf(stdout, NULL);

  // REPL loop: prompt -> read -> parse -> redirect -> execute -> restore.
  char command[MAX_COMMAND_LENGTH];
  while (1) {
    printf("$ ");
    if (read_command_line(command, sizeof(command)) < 0) {
      break;
    }

    // 1) Tokenize command line into raw shell arguments.
    char *args[MAX_ARGS];
    int arg_count = parse_arguments(command, args, MAX_ARGS);
    char *command_args[MAX_ARGS];
    RedirectionSpec redir;
    SavedDescriptors saved;

    if (arg_count == 0) {
      continue; // 如果没有输入命令，继续下一轮循环
    }

    // 2) Separate redirection operators from command arguments.
    int command_arg_count = split_command_and_redirections(
        args, arg_count, command_args, MAX_ARGS, &redir);
    if (command_arg_count <= 0) {
      continue;
    }

    // 3) Apply fd redirections before executing the command.
    if (apply_redirections(&redir, &saved) < 0) {
      continue;
    }

    // 4) Execute builtin/external command.
    int should_exit = execute_command(command_args, command_arg_count);

    // 5) Always restore stdout/stderr for the next prompt.
    restore_redirections(&saved);

    if (should_exit) {
      break;
    }
  }

  restore_interactive_input_mode(&terminal_mode);

  return 0;
}
