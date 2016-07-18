#include <dirent.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include <errno.h>

#include "libc.h"

static ssize_t stub_read(int fd, void* buf, size_t count) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_read, read);

static ssize_t stub_write(int fd, const void* buf, size_t count) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_write, write);

static int stub_close(int fd) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_close, close);

static int stub_open(const char* path, int flags, ...) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_open, open);

static off_t stub_lseek(int fd, off_t offset, int whence) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_lseek, lseek);

static int stub_isatty(int fd) {
    errno = ENOSYS;
    return 0;
}
weak_alias(stub_isatty, isatty);

static ssize_t stub_readv(int fd, const struct iovec* iov, int num) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_readv, readv);

static ssize_t stub_writev(int fd, const struct iovec* iov, int num) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_writev, writev);

static int stub_link(const char* oldpath, const char* newpath) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_link, link);

static int stub_unlinkat(int fd, const char* path, int flag) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_unlinkat, unlinkat);

static int stub_unlink(const char* path) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_unlink, unlink);

static ssize_t stub_readlink(const char* path, char* buf, size_t bufsiz) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_readlink, readlink);

static int stub_mkdir(const char* path, mode_t mode) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_mkdir, mkdir);

static int stub_rmdir(const char* path) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_rmdir, rmdir);

static char *stub_getcwd(char* buf, size_t size) {
    errno = ENOSYS;
    return NULL;
}
weak_alias(stub_getcwd, getcwd);

static int stub_fstat(int fd, struct stat* s) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_fstat, fstat);

static int stub_stat(const char* fn, struct stat* s) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_stat, stat);

static int stub_pipe(int pipefd[2]) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_pipe, pipe);

static int stub_chdir(const char* path) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_chdir, chdir);

static DIR* stub_opendir(const char* name) {
    errno = ENOSYS;
    return NULL;
}
weak_alias(stub_opendir, opendir);

static int stub_closedir(DIR* dir) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_closedir, closedir);

static struct dirent* stub_readdir(DIR* dir) {
    errno = ENOSYS;
    return NULL;
}
weak_alias(stub_readdir, readdir);
