// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netsvc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <inet6/inet6.h>
#include <inet6/netifc.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <launchpad/launchpad.h>

#include <magenta/boot/netboot.h>

#define TMP_SUFFIX ".netsvc.tmp"

netfile_state netfile = {
    .fd = -1,
    .needs_rename = false,
};

static int netfile_mkdir(const char* filename) {
    const char* ptr = filename[0] == '/' ? filename + 1 : filename;
    struct stat st;
    char tmp[1024];
    for (;;) {
        ptr = strchr(ptr, '/');
        if (!ptr) {
            return 0;
        }
        memcpy(tmp, filename, ptr - filename);
        tmp[ptr - filename] = '\0';
        ptr += 1;
        if (stat(tmp, &st) < 0) {
            if (errno == ENOENT) {
                if (mkdir(tmp, 0755) < 0) {
                    return -1;
                }
            } else {
                return -1;
            }
        }
    }
}

int netfile_open(const char *filename, uint32_t arg) {
    if (netfile.fd >= 0) {
        printf("netsvc: closing still-open '%s', replacing with '%s'\n", netfile.filename, filename);
        close(netfile.fd);
        netfile.fd = -1;
    }
    size_t len = strlen(filename);
    strlcpy(netfile.filename, filename, sizeof(netfile.filename));

    struct stat st;
again: // label here to catch filename=/path/to/new/directory/
    if (stat(filename, &st) == 0 && S_ISDIR(st.st_mode)) {
        errno = EISDIR;
        goto err;
    }

    switch (arg) {
    case O_RDONLY:
        netfile.needs_rename = false;
        netfile.fd = open(filename, O_RDONLY);
        break;
    case O_WRONLY: {
        // If we're writing a file, actually write to "filename + TMP_SUFFIX",
        // and rename to the final destination when we would close. This makes
        // written files appear to atomically update.
        if (len + strlen(TMP_SUFFIX) + 1 > PATH_MAX) {
            errno = ENAMETOOLONG;
            goto err;
        }
        strcat(netfile.filename, TMP_SUFFIX);
        netfile.needs_rename = true;
        netfile.fd = open(netfile.filename, O_WRONLY|O_CREAT|O_TRUNC);
        netfile.filename[len] = '\0';
        if (netfile.fd < 0 && errno == ENOENT) {
            if (netfile_mkdir(filename) == 0) {
                goto again;
            }
        }
        break;
    }
    default:
        printf("netsvc: open '%s' with invalid mode %d\n", filename, arg);
        errno = EINVAL;
    }
    if (netfile.fd < 0) {
        goto err;
    } else {
        strlcpy(netfile.filename, filename, sizeof(netfile.filename));
        netfile.offset = 0;
    }

    return 0;
err:
    netfile.filename[0] = '\0';
    return -errno;
}

int netfile_offset_read(void* data_out, off_t offset, size_t max_len) {
    if (netfile.fd < 0) {
        printf("netsvc: read, but no open file\n");
        return -EBADF;
    }
    if (offset != netfile.offset) {
        if (lseek(netfile.fd, offset, SEEK_SET) != offset) {
            return -errno;
        }
        netfile.offset = offset;
    }
    return netfile_read(data_out, max_len);
}

int netfile_read(void *data_out, size_t data_sz) {
    if (netfile.fd < 0) {
        printf("netsvc: read, but no open file\n");
        return -EBADF;
    }
    ssize_t n = read(netfile.fd, data_out, data_sz);
    if (n < 0) {
        printf("netsvc: error reading '%s': %d\n", netfile.filename, errno);
        int result = (errno == 0) ? -EIO : -errno;
        close(netfile.fd);
        netfile.fd = -1;
        return result;
    }
    netfile.offset += n;
    return n;
}

int netfile_offset_write(const char* data, off_t offset, size_t length) {
    if (netfile.fd < 0) {
        printf("netsvc: write, but no open file\n");
        return -EBADF;
    }
    if (offset != netfile.offset) {
        if (lseek(netfile.fd, offset, SEEK_SET) != offset) {
            return -errno;
        }
        netfile.offset = offset;
    }
    return netfile_write(data, length);
}

int netfile_write(const char* data, size_t len) {
    if (netfile.fd < 0) {
        printf("netsvc: write, but no open file\n");
        return -EBADF;
    }
    ssize_t n = write(netfile.fd, data, len);
    if (n != (ssize_t)len) {
        printf("netsvc: error writing %s: %d\n", netfile.filename, errno);
        int result = (errno == 0) ? -EIO : -errno;
        close(netfile.fd);
        netfile.fd = -1;
        return result;
    }
    netfile.offset += len;
    return len;
}

int netfile_close(void) {
    int result = 0;
    if (netfile.fd < 0) {
        printf("netsvc: close, but no open file\n");
    } else {
        if (netfile.needs_rename) {
            char src[PATH_MAX];
            strlcpy(src, netfile.filename, sizeof(src));
            strlcat(src, TMP_SUFFIX, sizeof(src));
            if (rename(src, netfile.filename)) {
                printf("netsvc: failed to rename temporary file: %s\n", strerror(errno));
            }
        }
        if (close(netfile.fd)) {
            result = (errno == 0) ? -EIO : -errno;
        }
        netfile.fd = -1;
    }
    return result;
}

// Clean up if we abort before finishing a write. Close out and unlink it, rather than
// leaving an incomplete file.
void netfile_abort_write(void) {
    if (netfile.fd < 0) {
        return;
    }
    close(netfile.fd);
    netfile.fd = -1;
    char tmp[PATH_MAX];
    const char* filename;
    if (netfile.needs_rename) {
        strlcpy(tmp, netfile.filename, sizeof(tmp));
        strlcat(tmp, TMP_SUFFIX, sizeof(tmp));
        filename = tmp;
    } else {
        filename = netfile.filename;
    }
    if (unlink(filename) != 0) {
        printf("netsvc: failed to unlink aborted file %s\n", filename);
    }
}
