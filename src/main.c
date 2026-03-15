#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char *arg = strtok(NULL, "");

    if(strcmp(cmd, "exit") == 0) {
      break;
    } else if(strcmp(cmd, "echo") == 0) {
      printf("%s\r\n", arg);
    } else if(strcmp(cmd, "type") == 0) {
      if(!strcmp(arg, "exit") || !strcmp(arg, "echo") || !strcmp(arg, "type")) {
        printf("%s is a shell builtin\r\n", arg);
      } else {
        printf("%s: not found\r\n", arg);
      }
    } else {
      printf("%s: command not found\r\n", cmd);
    }
  }
  
  return 0;
}
