#ifndef __ZUTIL_H
#define __ZUTIL_H

/* necessary stuff to transplant crc32 and adler32 from zlib */
#include <inttypes.h>
#include <stddef.h>
#include <sys/types.h>

#define Z_NULL NULL
#define OF(args) args
#define ZEXPORT

#endif

