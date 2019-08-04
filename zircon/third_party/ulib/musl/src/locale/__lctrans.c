#include <locale.h>

#include "libc.h"
#include "locale_impl.h"

const char* __lctrans_cur(const char* msg) {
  return __lctrans(msg, CURRENT_LOCALE->cat[LC_MESSAGES]);
}
