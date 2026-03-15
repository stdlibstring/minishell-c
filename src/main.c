#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  printf("$ ");

  char command[256];
  fgets(command, sizeof(command), stdin);

  // 去除多余的换行符
  command[strcspn(command, "\r\n")] = 0;
  printf("%s: command not found\r\n", command);
  return 0;
}
