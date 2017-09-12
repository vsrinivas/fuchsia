#pragma once

#include "libc.h"
#include <stdatomic.h>
#include <stdio.h>

#define UNGET 8

#define FFINALLOCK(f) ((f)->lock >= 0 ? __lockfile((f)) : 0)
#define FLOCK(f) int __need_unlock = ((f)->lock >= 0 ? __lockfile((f)) : 0)
#define FUNLOCK(f)     \
    if (__need_unlock) \
    __unlockfile((f))

#define F_PERM 1
#define F_NORD 4
#define F_NOWR 8
#define F_EOF 16
#define F_ERR 32
#define F_SVB 64
#define F_APP 128

struct _IO_FILE {
    unsigned flags;
    unsigned char *rpos, *rend;
    int (*close)(FILE*);
    unsigned char *wend, *wpos;
    unsigned char* mustbezero_1;
    unsigned char* wbase;
    size_t (*read)(FILE*, unsigned char*, size_t);
    size_t (*write)(FILE*, const unsigned char*, size_t);
    off_t (*seek)(FILE*, off_t, int);
    unsigned char* buf;
    size_t buf_size;
    FILE *prev, *next;
    int fd;
    int pipe_pid;
    long lockcount;
    short dummy3;
    signed char mode;
    signed char lbf;
    atomic_int lock;
    atomic_int waiters;
    void* cookie;
    off_t off;
    char* getln_buf;
    void* mustbezero_2;
    unsigned char* shend;
    off_t shlim, shcnt;
    struct __locale_struct* locale;
};

size_t __stdio_read(FILE*, unsigned char*, size_t) ATTR_LIBC_VISIBILITY;
size_t __stdio_write(FILE*, const unsigned char*, size_t) ATTR_LIBC_VISIBILITY;
size_t __stdout_write(FILE*, const unsigned char*, size_t) ATTR_LIBC_VISIBILITY;
off_t __stdio_seek(FILE*, off_t, int) ATTR_LIBC_VISIBILITY;
int __stdio_close(FILE*) ATTR_LIBC_VISIBILITY;

size_t __string_read(FILE*, unsigned char*, size_t) ATTR_LIBC_VISIBILITY;

int __toread(FILE*) ATTR_LIBC_VISIBILITY;
int __towrite(FILE*) ATTR_LIBC_VISIBILITY;

zx_status_t _mmap_file(size_t offset, size_t len, uint32_t zx_flags, int flags, int fd,
                       off_t fd_off, uintptr_t* out);

#if defined(__PIC__) && (100 * __GNUC__ + __GNUC_MINOR__ >= 303)
__attribute__((visibility("protected")))
#endif
int
__overflow(FILE *, int),
    __uflow(FILE *);

int __fseeko(FILE*, off_t, int) ATTR_LIBC_VISIBILITY;
int __fseeko_unlocked(FILE*, off_t, int) ATTR_LIBC_VISIBILITY;
off_t __ftello(FILE*) ATTR_LIBC_VISIBILITY;
off_t __ftello_unlocked(FILE*) ATTR_LIBC_VISIBILITY;
size_t __fwritex(const unsigned char*, size_t, FILE*) ATTR_LIBC_VISIBILITY;
int __putc_unlocked(int, FILE*) ATTR_LIBC_VISIBILITY;

FILE* __fdopen(int, const char*) ATTR_LIBC_VISIBILITY;
int __fmodeflags(const char*) ATTR_LIBC_VISIBILITY;

FILE* __ofl_add(FILE* f) ATTR_LIBC_VISIBILITY;
FILE** __ofl_lock(void) ATTR_LIBC_VISIBILITY;
void __ofl_unlock(void) ATTR_LIBC_VISIBILITY;

void __stdio_exit(void) ATTR_LIBC_VISIBILITY;

#define feof(f) ((f)->flags & F_EOF)
#define ferror(f) ((f)->flags & F_ERR)

#define getc_unlocked(f) (((f)->rpos < (f)->rend) ? *(f)->rpos++ : __uflow((f)))

#define putc_unlocked(c, f)                                                         \
    (((unsigned char)(c) != (f)->lbf && (f)->wpos < (f)->wend) ? *(f)->wpos++ = (c) \
                                                               : __overflow((f), (c)))

/* Caller-allocated FILE * operations */
FILE* __fopen_rb_ca(const char*, FILE*, unsigned char*, size_t)
    ATTR_LIBC_VISIBILITY;
int __fclose_ca(FILE*) ATTR_LIBC_VISIBILITY;
