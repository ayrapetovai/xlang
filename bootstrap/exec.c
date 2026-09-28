#include "exec.h"
#include <stdio.h>

int exec(const char *filename) {
  printf("exec %s\n", filename);
  return 0;
}
