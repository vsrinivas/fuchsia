#include "libc.h"
#include <errno.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>

int __statfs(const char* path, struct statfs* buf) {
    *buf = (struct statfs){};
    errno = ENOSYS;
    return -1;
}

int __fstatfs(int fd, struct statfs* buf) {
    *buf = (struct statfs){};
    errno = ENOSYS;
    return -1;
}

weak_alias(__statfs, statfs);
weak_alias(__fstatfs, fstatfs);

static void fixup(struct statvfs* out, const struct statfs* in) {
    *out = (struct statvfs){};
    out->f_bsize = in->f_bsize;
    out->f_frsize = in->f_frsize ? in->f_frsize : in->f_bsize;
    out->f_blocks = in->f_blocks;
    out->f_bfree = in->f_bfree;
    out->f_bavail = in->f_bavail;
    out->f_files = in->f_files;
    out->f_ffree = in->f_ffree;
    out->f_favail = in->f_ffree;
    out->f_fsid = in->f_fsid.__val[0];
    out->f_flag = in->f_flags;
    out->f_namemax = in->f_namelen;
}

int statvfs(const char* restrict path, struct statvfs* restrict buf) {
    struct statfs kbuf;
    if (__statfs(path, &kbuf) < 0)
        return -1;
    fixup(buf, &kbuf);
    return 0;
}

int fstatvfs(int fd, struct statvfs* buf) {
    struct statfs kbuf;
    if (__fstatfs(fd, &kbuf) < 0)
        return -1;
    fixup(buf, &kbuf);
    return 0;
}
