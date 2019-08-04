#include "time_impl.h"

char* asctime(const struct tm* tm) {
  static char buf[26];
  return __asctime(tm, buf);
}
