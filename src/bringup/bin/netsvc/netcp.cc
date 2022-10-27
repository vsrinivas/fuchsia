// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/netsvc/netcp.h"

#include <errno.h>
#include <fcntl.h>
#include <lib/netboot/netboot.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

namespace {

constexpr char TMP_SUFFIX[] = ".netsvc.tmp";

struct netcp_state {
  int fd;
  off_t offset;
  // false: Filename is the open file and final destination
  // true : Filename is final destination; open file has a magic tmp suffix
  bool needs_rename;
  char filename[PATH_MAX];
};

netcp_state netcp = {
    .fd = -1,
    .offset = 0,
    .needs_rename = false,
    .filename = {0},
};

int netcp_mkdir(const char* filename) {
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

}  // namespace

int netcp_open(const char* filename, uint32_t arg, size_t* file_size) {
  if (netcp.fd >= 0) {
    printf("netsvc: closing still-open '%s', replacing with '%s'\n", netcp.filename, filename);
    close(netcp.fd);
    netcp.fd = -1;
  }
  size_t len = strlen(filename);
  strlcpy(netcp.filename, filename, sizeof(netcp.filename));

  struct stat st;
again:  // label here to catch filename=/path/to/new/directory/
  if (stat(filename, &st) == 0 && S_ISDIR(st.st_mode)) {
    errno = EISDIR;
    goto err;
  }

  switch (arg) {
    case O_RDONLY:
      netcp.needs_rename = false;
      netcp.fd = open(filename, O_RDONLY);
      if (file_size) {
        *file_size = st.st_size;
      }
      break;
    case O_WRONLY: {
      // If we're writing a file, actually write to "filename + TMP_SUFFIX",
      // and rename to the final destination when we would close. This makes
      // written files appear to atomically update.
      if (len + strlen(TMP_SUFFIX) + 1 > PATH_MAX) {
        errno = ENAMETOOLONG;
        goto err;
      }
      strcat(netcp.filename, TMP_SUFFIX);
      netcp.needs_rename = true;
      netcp.fd = open(netcp.filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
      netcp.filename[len] = '\0';
      if (netcp.fd < 0 && errno == ENOENT) {
        if (netcp_mkdir(filename) == 0) {
          goto again;
        }
      }
      break;
    }
    default:
      printf("netsvc: open '%s' with invalid mode %d\n", filename, arg);
      errno = EINVAL;
  }
  if (netcp.fd < 0) {
    goto err;
  } else {
    strlcpy(netcp.filename, filename, sizeof(netcp.filename));
    netcp.offset = 0;
  }

  return 0;
err:
  netcp.filename[0] = '\0';
  return -errno;
}

ssize_t netcp_offset_read(void* data_out, off_t offset, size_t max_len) {
  if (netcp.fd < 0) {
    printf("netsvc: read, but no open file\n");
    return -EBADF;
  }
  if (offset != netcp.offset) {
    if (lseek(netcp.fd, offset, SEEK_SET) != offset) {
      return -errno;
    }
    netcp.offset = offset;
  }
  return netcp_read(data_out, max_len);
}

ssize_t netcp_read(void* data_out, size_t data_sz) {
  if (netcp.fd < 0) {
    printf("netsvc: read, but no open file\n");
    return -EBADF;
  }
  ssize_t n = read(netcp.fd, data_out, data_sz);
  if (n < 0) {
    printf("netsvc: error reading '%s': %d\n", netcp.filename, errno);
    int result = (errno == 0) ? -EIO : -errno;
    close(netcp.fd);
    netcp.fd = -1;
    return result;
  }
  netcp.offset += n;
  return n;
}

ssize_t netcp_offset_write(const char* data, off_t offset, size_t length) {
  if (netcp.fd < 0) {
    printf("netsvc: write, but no open file\n");
    return -EBADF;
  }
  if (offset != netcp.offset) {
    if (lseek(netcp.fd, offset, SEEK_SET) != offset) {
      return -errno;
    }
    netcp.offset = offset;
  }
  return netcp_write(data, length);
}

ssize_t netcp_write(const char* data, size_t len) {
  if (netcp.fd < 0) {
    printf("netsvc: write, but no open file\n");
    return -EBADF;
  }
  ssize_t n = write(netcp.fd, data, len);
  if (n != static_cast<ssize_t>(len)) {
    printf("netsvc: error writing %s: %d\n", netcp.filename, errno);
    int result = (errno == 0) ? -EIO : -errno;
    close(netcp.fd);
    netcp.fd = -1;
    return result;
  }
  netcp.offset += len;
  return len;
}

int netcp_close() {
  int result = 0;
  if (netcp.fd < 0) {
    printf("netsvc: close, but no open file\n");
  } else {
    if (netcp.needs_rename) {
      char src[PATH_MAX];
      strlcpy(src, netcp.filename, sizeof(src));
      strlcat(src, TMP_SUFFIX, sizeof(src));
      if (rename(src, netcp.filename)) {
        printf("netsvc: failed to rename temporary file: %s\n", strerror(errno));
      }
    }
    if (close(netcp.fd)) {
      result = (errno == 0) ? -EIO : -errno;
    }
    netcp.fd = -1;
  }
  return result;
}

// Clean up if we abort before finishing a write. Close out and unlink it, rather than
// leaving an incomplete file.
void netcp_abort_write() {
  if (netcp.fd < 0) {
    return;
  }
  close(netcp.fd);
  netcp.fd = -1;
  char tmp[PATH_MAX];
  const char* filename;
  if (netcp.needs_rename) {
    strlcpy(tmp, netcp.filename, sizeof(tmp));
    strlcat(tmp, TMP_SUFFIX, sizeof(tmp));
    filename = tmp;
  } else {
    filename = netcp.filename;
  }
  if (unlink(filename) != 0) {
    printf("netsvc: failed to unlink aborted file %s\n", filename);
  }
}
