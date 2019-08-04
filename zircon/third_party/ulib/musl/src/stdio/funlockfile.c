#include "stdio_impl.h"

void funlockfile(FILE* f) {
  if (--f->lockcount == 0) {
    __unlockfile(f);
  }
}
