#include "__dirent.h"
#include "libc.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>

int readdir_r(DIR* restrict dir, struct dirent* restrict buf, struct dirent** restrict result) {
    struct dirent* de;
    int errno_save = errno;
    int ret;

    mxr_mutex_lock(&dir->lock);
    errno = 0;
    de = readdir(dir);
    if ((ret = errno)) {
        mxr_mutex_unlock(&dir->lock);
        return ret;
    }
    errno = errno_save;
    if (de)
        memcpy(buf, de, de->d_reclen);
    else
        buf = NULL;

    mxr_mutex_unlock(&dir->lock);
    *result = buf;
    return 0;
}

LFS64_2(readdir_r, readdir64_r);
