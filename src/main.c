#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_COMMAND_LENGTH 256
#define MAX_ARGS 128

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
    while (*read == ' ' || *read == '\t') {
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

    while (*read != '\0') {
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

      if (!in_single_quote && !in_double_quote &&
          (*read == ' ' || *read == '\t')) {
        while (*read == ' ' || *read == '\t') {
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

    if (arg_count == 0) {
      continue; // 如果没有输入命令，继续下一轮循环
    }

    if (execute_command(args, arg_count)) {
      break;
    }
  }

  return 0;
}
