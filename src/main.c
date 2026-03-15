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

static void restore_fd(int saved_fd, int target_fd);

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

// Try to autocomplete the first command word when TAB is pressed.
// Only "echo" and "exit" are considered in this stage.
static void autocomplete_builtin_live(char *line, size_t *len,
                                      size_t line_size) {
  size_t word_start = 0;
  while (word_start < *len && line[word_start] == ' ') {
    word_start++;
  }

  size_t word_end = word_start;
  while (word_end < *len && line[word_end] != ' ' && line[word_end] != '\t') {
    word_end++;
  }

  // Only complete when cursor is still on the first word.
  if (word_end != *len || word_start == word_end) {
    return;
  }

  static const char *candidates[] = {"echo", "exit"};
  size_t partial_len = word_end - word_start;
  const char *matched = NULL;
  int match_count = 0;

  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    if (starts_with(candidates[i], line + word_start, partial_len)) {
      matched = candidates[i];
      match_count++;
    }
  }

  if (match_count != 1) {
    return;
  }

  size_t matched_len = strlen(matched);
  size_t add_len = (matched_len - partial_len) + 1; // + trailing space
  if (*len + add_len >= line_size) {
    return;
  }

  // Print only appended characters to emulate shell live completion.
  for (size_t i = partial_len; i < matched_len; i++) {
    line[(*len)++] = matched[i];
    putchar(matched[i]);
  }
  line[(*len)++] = ' ';
  putchar(' ');
  line[*len] = '\0';
}

// Read one command line from stdin in a key-by-key fashion so TAB completion
// can happen immediately after the key press.
static int read_command_line(char *line, size_t line_size) {
  size_t len = 0;
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
      autocomplete_builtin_live(line, &len, line_size);
      continue;
    }

    // Basic backspace support for interactive editing.
    if (ch == 127 || ch == '\b') {
      if (len > 0) {
        len--;
        line[len] = '\0';
        printf("\b \b");
      }
      continue;
    }

    if (len + 1 >= line_size) {
      continue;
    }

    line[len++] = ch;
    line[len] = '\0';
    putchar(ch);
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

  char *path_env = getenv("PATH");
  if (path_env == NULL || *path_env == '\0') {
    return 0;
  }

  size_t path_len = strlen(path_env);
  char *path_copy = malloc(path_len + 1);
  if (path_copy == NULL) {
    return 0;
  }
  memcpy(path_copy, path_env, path_len + 1);

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

    if (strcmp(token, ">>") == 0 || strcmp(token, "1>>") == 0) {
      if (i + 1 >= arg_count) {
        return -1;
      }
      redir->stdout_file = args[++i];
      redir->stdout_append = 1;
      continue;
    }

    if (strcmp(token, "2>>") == 0) {
      if (i + 1 >= arg_count) {
        return -1;
      }
      redir->stderr_file = args[++i];
      redir->stderr_append = 1;
      continue;
    }

    if (strncmp(token, "1>>", 3) == 0 && token[3] != '\0') {
      redir->stdout_file = token + 3;
      redir->stdout_append = 1;
      continue;
    }

    if (strncmp(token, ">>", 2) == 0 && token[2] != '\0') {
      redir->stdout_file = token + 2;
      redir->stdout_append = 1;
      continue;
    }

    if (strncmp(token, "2>>", 3) == 0 && token[3] != '\0') {
      redir->stderr_file = token + 3;
      redir->stderr_append = 1;
      continue;
    }

    if (strcmp(token, ">") == 0 || strcmp(token, "1>") == 0) {
      if (i + 1 >= arg_count) {
        return -1;
      }
      redir->stdout_file = args[++i];
      redir->stdout_append = 0;
      continue;
    }

    if (strcmp(token, "2>") == 0) {
      if (i + 1 >= arg_count) {
        return -1;
      }
      redir->stderr_file = args[++i];
      redir->stderr_append = 0;
      continue;
    }

    if (strncmp(token, "1>", 2) == 0 && token[2] != '\0') {
      redir->stdout_file = token + 2;
      redir->stdout_append = 0;
      continue;
    }

    if (token[0] == '>' && token[1] != '\0') {
      redir->stdout_file = token + 1;
      redir->stdout_append = 0;
      continue;
    }

    if (strncmp(token, "2>", 2) == 0 && token[2] != '\0') {
      redir->stderr_file = token + 2;
      redir->stderr_append = 0;
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
