#include "time_impl.h"

char* asctime_r(const struct tm* restrict tm, char* restrict buf) { return __asctime(tm, buf); }
