// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/uio.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <unittest/unittest.h>

// output via debuglog syscalls

static mx_handle_t log_handle;

#define LOGBUF_MAX (MX_LOG_RECORD_MAX - sizeof(mx_log_record_t))

static void log_write(const void* data, size_t len) {
    while (len > 0) {
        size_t xfer = (len > LOGBUF_MAX) ? LOGBUF_MAX : len;
        mx_log_write(log_handle, xfer, data, 0);
        data += xfer;
        len -= xfer;
    }
}


// libc init and io stubs
// The reason these are here is that the "core" tests intentionally do not
// use mxio. See ./README.md.

void __libc_extensions_init(mx_proc_info_t* pi) {
}

ssize_t write(int fd, const void* data, size_t count) {
    if ((fd == 1) || (fd == 2)) {
        log_write(data, count);
    }
    return count;
}

ssize_t readv(int fd, const struct iovec* iov, int num) {
    return 0;
}

ssize_t writev(int fd, const struct iovec* iov, int num) {
    ssize_t count = 0;
    ssize_t r;
    while (num > 0) {
        if (iov->iov_len != 0) {
            r = write(fd, iov->iov_base, iov->iov_len);
            if (r < 0) {
                return count ? count : r;
            }
            if ((size_t)r < iov->iov_len) {
                return count + r;
            }
            count += r;
        }
        iov++;
        num--;
    }
    return count;
}

#define ERROR() do { errno = ENOSYS; return -1; } while (0)

off_t lseek(int fd, off_t offset, int whence) {
    ERROR();
}

int isatty(int fd) {
    return 1;
}


int main(int argc, char** argv) {
    if ((log_handle = mx_log_create(0)) < 0) {
        return -2;
    }
    mx_log_write(log_handle, 4, "TEST", 0);
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
