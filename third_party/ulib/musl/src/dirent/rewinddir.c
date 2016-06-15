#include "__dirent.h"
#include "libc.h"
#include <dirent.h>
#include <unistd.h>

void rewinddir(DIR* dir) {
    mxr_mutex_lock(&dir->lock);
    lseek(dir->fd, 0, SEEK_SET);
    dir->buf_pos = dir->buf_end = 0;
    dir->tell = 0;
    mxr_mutex_unlock(&dir->lock);
}
