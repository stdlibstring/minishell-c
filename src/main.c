#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_COMMAND_LENGTH 256
#define MAX_ARGS 128

static int is_inline_whitespace(char c) { return c == ' ' || c == '\t'; }

static int is_escapable_in_double_quotes(char c) {
  return c == '"' || c == '\\';
}

static int is_builtin(const char *cmd) {
  return strcmp(cmd, "echo") == 0 || strcmp(cmd, "type") == 0 ||
         strcmp(cmd, "exit") == 0 || strcmp(cmd, "pwd") == 0 ||
         strcmp(cmd, "cd") == 0;
}

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

static int split_command_and_stdout_redirection(char **args, int arg_count,
                                                char **command_args,
                                                int max_args,
                                                char **stdout_file) {
  int command_arg_count = 0;
  *stdout_file = NULL;

  for (int i = 0; i < arg_count; i++) {
    char *token = args[i];

    if (strcmp(token, ">") == 0 || strcmp(token, "1>") == 0) {
      if (i + 1 >= arg_count) {
        return -1;
      }
      *stdout_file = args[++i];
      continue;
    }

    if (strncmp(token, "1>", 2) == 0 && token[2] != '\0') {
      *stdout_file = token + 2;
      continue;
    }

    if (token[0] == '>' && token[1] != '\0') {
      *stdout_file = token + 1;
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

static int redirect_stdout_to_file(const char *path) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return -1;
  }

  int saved_stdout = dup(STDOUT_FILENO);
  if (saved_stdout < 0) {
    close(fd);
    return -1;
  }

  if (dup2(fd, STDOUT_FILENO) < 0) {
    close(fd);
    close(saved_stdout);
    return -1;
  }

  close(fd);
  return saved_stdout;
}

static void restore_stdout(int saved_stdout) {
  fflush(stdout);
  dup2(saved_stdout, STDOUT_FILENO);
  close(saved_stdout);
}

static void handle_echo(char **args, int arg_count) {
  for (int i = 1; i < arg_count; i++) {
    if (i > 1) {
      printf(" ");
    }
    printf("%s", args[i]);
  }
  printf("\r\n");
}

static void handle_pwd(void) {
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("%s\r\n", cwd);
  } else {
    perror("pwd");
  }
}

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

  // Flush after every printf
  setbuf(stdout, NULL);

  char command[MAX_COMMAND_LENGTH];
  while (1) {
    printf("$ ");
    if (fgets(command, sizeof(command), stdin) == NULL) {
      break;
    }

    // 去除多余的换行符
    command[strcspn(command, "\r\n")] = 0;
    char *args[MAX_ARGS];
    int arg_count = parse_arguments(command, args, MAX_ARGS);
    char *command_args[MAX_ARGS];
    char *stdout_file = NULL;

    if (arg_count == 0) {
      continue; // 如果没有输入命令，继续下一轮循环
    }

    int command_arg_count = split_command_and_stdout_redirection(
        args, arg_count, command_args, MAX_ARGS, &stdout_file);
    if (command_arg_count <= 0) {
      continue;
    }

    int saved_stdout = -1;
    if (stdout_file != NULL) {
      saved_stdout = redirect_stdout_to_file(stdout_file);
      if (saved_stdout < 0) {
        continue;
      }
    }

    int should_exit = execute_command(command_args, command_arg_count);

    if (saved_stdout >= 0) {
      restore_stdout(saved_stdout);
    }

    if (should_exit) {
      break;
    }
  }

  return 0;
}
