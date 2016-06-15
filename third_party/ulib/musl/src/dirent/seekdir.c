#include "__dirent.h"
#include "libc.h"
#include <dirent.h>
#include <unistd.h>

void seekdir(DIR* dir, long off) {
    mxr_mutex_lock(&dir->lock);
    dir->tell = lseek(dir->fd, off, SEEK_SET);
    dir->buf_pos = dir->buf_end = 0;
    mxr_mutex_unlock(&dir->lock);
}
