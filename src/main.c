#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

static int is_builtin(const char *cmd) {
  return strcmp(cmd, "echo") == 0 || strcmp(cmd, "type") == 0 || strcmp(cmd, "exit") == 0;
}

static void handle_type(const char *name) {
  if(name == NULL || *name == '\0') {
    printf("type: missing operand\r\n");
    return;
  }

  // 1) builtin
  if(is_builtin(name)) {
    printf("%s is a shell builtin\r\n", name);
    return;
  }

  // 2) search PATH
  char *path_env = getenv("PATH");
  if(path_env == NULL || *path_env == '\0') {
    printf("%s not found\r\n", name);
    return;
  }

  char *path = strdup(path_env);
  if (path == NULL) {
    printf("%s: not found\r\n", name);
    return;
  }

  for(char *dir = strtok(path, ":"); dir != NULL; dir = strtok(NULL, ":")) {
    char full_path[PATH_MAX];
    // 构造完整路径
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, name);

    struct stat st;
    if(stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
      if(access(full_path, X_OK) == 0) {
      printf("%s is %s\r\n", name, full_path);
      free(path);
      return;
      }
    } 
  }

  printf("%s not found\r\n", name);
  free(path);
}
int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  // printf("$ ");

  char command[256];
  while(1){
    printf("$ ");
    fgets(command, sizeof(command), stdin);

    // 去除多余的换行符
    command[strcspn(command, "\r\n")] = 0;

    char *cmd = strtok(command, " \r\n");
    

    if(cmd == NULL) {
      continue; // 如果没有输入命令，继续下一轮循环
    }

    if(strcmp(cmd, "exit") == 0) {
      break;
    } else if(strcmp(cmd, "echo") == 0) {
      char *arg = strtok(NULL, "");
      printf("%s\r\n", arg ? arg : "");
    } else if(strcmp(cmd, "type") == 0) {
      char *arg = strtok(NULL, " \t");
      handle_type(arg);
    } else {
      printf("%s: command not found\r\n", cmd);
    }
  }
  
  return 0;
}
