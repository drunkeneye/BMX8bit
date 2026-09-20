#include <errno.h>

extern "C" int *__errno_location(void) {
  return &errno;
}

extern "C" int _getpid(void) {
  return 100;
}
