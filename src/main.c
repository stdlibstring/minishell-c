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

    if(strcmp(command, "exit") == 0) {
      break;
    } else if(strncmp(command, "echo ", 5) == 0) {
      printf("%s\r\n", command + 5);
    } else if(strncmp(command, "type ", 5) == 0) {
      if(strcmp(command + 5, "exit") == 0) {
        printf("exit is a shell builtin\r\n");
      } else if(strcmp(command + 5, "echo") == 0) {
        printf("echo is a shell builtin\r\n");
      } else if(strcmp(command + 5, "type") == 0) {
        printf("type is a shell builtin\r\n");
      } else {
        printf("%s: not found\r\n", command);
      }
    } else {
      printf("%s: command not found\r\n", command);
    }
  }
  
  return 0;
}
