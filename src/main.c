#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  // TODO: Uncomment the code below to pass the first stage
  printf("$ ");

  char command[256];
  fgets(command, sizeof(command), stdin);

  // 去除多余的换行符
  command[strcspn(command, "\r\n")] = 0;

  // 简单的命令处理逻辑
  printf("%s: command not found\r\n", command);
  return 0;
}
