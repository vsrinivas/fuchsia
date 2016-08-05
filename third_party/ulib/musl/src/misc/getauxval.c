#include "libc.h"
#include <errno.h>
#include <sys/auxv.h>
#include <sys/param.h>

unsigned long getauxval(unsigned long item) {
    // We have no auxv, so just special-case a few things we do know about.
    // TODO(mcgrathr): Maybe handle some more values?  It's unclear what
    // any users want other than AT_HWCAP, which we don't have.

    switch (item) {
    case AT_SECURE:
        return libc.secure;
    case AT_PAGESZ:
        return PAGE_SIZE;
    }

    errno = ENOENT;
    return 0;
}
