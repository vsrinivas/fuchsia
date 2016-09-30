#include <dirent.h>
#include <fcntl.h>
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

static int stub_openat(int fd, const char* filename, int flags, ...) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_openat, openat);

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

static int stub_mkdirat(int fd, const char* path, mode_t mode) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_mkdirat, mkdirat);

static int stub_rmdir(const char* path) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_rmdir, rmdir);

static char* stub_getcwd(char* buf, size_t size) {
    errno = ENOSYS;
    return NULL;
}
weak_alias(stub_getcwd, getcwd);

static int stub_fstat(int fd, struct stat* s) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_fstat, fstat);

static int stub_fstatat(int fd, const char* restrict path, struct stat* restrict buf, int flag) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_fstatat, fstatat);

static int stub_stat(const char* fn, struct stat* s) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_stat, stat);

static int stub_lstat(const char* restrict path, struct stat* restrict buf) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_lstat, lstat);

static int stub_dup(int oldfd) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_dup, dup);

static int stub_dup2(int oldfd, int newfd) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_dup2, dup2);

static int stub_pipe(int pipefd[2]) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_pipe, pipe);

static int stub_pipe2(int pipe2fd[2], int flags) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_pipe2, pipe2);

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

static DIR* stub_fdopendir(int fd) {
    errno = ENOSYS;
    return NULL;
}
weak_alias(stub_fdopendir, fdopendir);

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

static int stub_access(const char* path, int mode) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_access, access);

static int stub_faccessat(int fd, const char* path, int amode, int flags) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_faccessat, faccessat);

static int stub_chmod(const char* path, mode_t mode) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_chmod, chmod);

static int stub_fchmod(int fd, mode_t mode) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_fchmod, fchmod);

static int stub_chown(const char* path, uid_t owner, gid_t group) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_chown, chown);

static int stub_fchown(int fd, uid_t owner, gid_t group) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_fchown, fchown);

static int stub_lchown(const char* path, uid_t owner, gid_t group) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_lchown, lchown);

static int stub_creat(const char* path, mode_t mode) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_creat, creat);

static int stub_fcntl(int fd, int cmd, ...) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_fcntl, fcntl);

static int stub_fdatasync(int fd) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_fdatasync, fdatasync);

static int stub_fsync(int fd) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_fsync, fsync);

static int stub_ftruncate(int fd, off_t length) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_ftruncate, ftruncate);

static int stub_truncate(const char* path, off_t length) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_truncate, truncate);

static int stub_mkfifo(const char* path, mode_t mode) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_mkfifo, mkfifo);

static int stub_mknod(const char* path, mode_t mode, dev_t dev) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_mknod, mknod);

static int stub_remove(const char* path) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_remove, remove);

static int stub_rename(const char* oldpath, const char* newpath) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_rename, rename);

static int stub_symlink(const char* oldpath, const char* newpath) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_symlink, symlink);

static void stub_sync(void) {
    errno = ENOSYS;
}
weak_alias(stub_sync, sync);

static mode_t stub_umask(mode_t mask) {
    errno = ENOSYS;
    return -1;
}
weak_alias(stub_umask, umask);
