// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <zircon/device/debug.h>
#include <zircon/types.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xdc-init.h"

static constexpr uint32_t BUFFER_SIZE = 10 * 1024;
static constexpr uint32_t DEFAULT_STREAM_ID = 1;

typedef struct {
    off_t file_size;
} file_header_t;

static void usage(const char* prog_name) {
    printf("usage:\n");
    printf("%s [options]\n", prog_name);
    printf("\nOptions\n");
    printf("  -i <stream id>  : ID of stream to transfer over, must be positive. Defaults to 1.\n"
           "  -f <filename>   : Name of file to write to or read from.\n"
           "  -d              : Download from xdc. This is the default if no mode is specified.\n"
           "  -u              : Upload to xdc.\n");
}

// Reads the file header from the xdc device and stores it in out_file_header.
static zx_status_t read_file_header(const fbl::unique_fd& xdc_fd, file_header_t* out_file_header) {
    unsigned char* buf = reinterpret_cast<unsigned char*>(out_file_header);

    ssize_t res;
    size_t total_read = 0;
    size_t len = sizeof(file_header_t);
    while ((total_read < len) &&
           ((res = read(xdc_fd.get(), buf + total_read, len - total_read)) != 0)) {
        if (res < 0) {
            printf("Fatal read error: %s\n", strerror(errno));
            return ZX_ERR_IO;
        }
        total_read += res;
    }
    if (total_read != len) {
        fprintf(stderr, "Malformed file header, only read %lu bytes, want %lu\n", total_read, len);
        return ZX_ERR_BAD_STATE;
    }
    return ZX_OK;
}

// Writes the file header to the xdc device and also stores it in out_file_header.
static zx_status_t write_file_header(const fbl::unique_fd& file_fd, fbl::unique_fd& xdc_fd,
                                     file_header_t* out_file_header) {
    struct stat s;
    if (fstat(file_fd.get(), &s) < 0) {
        fprintf(stderr, "could not get size of file, err: %s\n", strerror(errno));
        return ZX_ERR_IO;
    }
    file_header_t file_header = { .file_size = s.st_size };
    unsigned char* buf = reinterpret_cast<unsigned char*>(&file_header);
    ssize_t res = write(xdc_fd.get(), buf, sizeof(file_header));
    if (sizeof(res) != sizeof(file_header)) {
        fprintf(stderr, "Fatal write err: %s\n", strerror(errno));
        return ZX_ERR_IO;
    }
    ZX_DEBUG_ASSERT(out_file_header != nullptr);
    memcpy(out_file_header, &file_header, sizeof(file_header));
    return ZX_OK;
}

// Reads from the src_fd and writes to the dest_fd until src_len bytes has been written,
// or a fatal error occurs while reading or writing.
static zx_status_t transfer(fbl::unique_fd& src_fd, off_t src_len, fbl::unique_fd& dest_fd) {
    printf("Transferring file of size %lld bytes.\n", src_len);

    fbl::unique_ptr<unsigned char*[]> buf(new unsigned char*[BUFFER_SIZE]);
    ssize_t res;
    off_t total_read = 0;
    while ((total_read < src_len) &&
           ((res = read(src_fd.get(), buf.get(), BUFFER_SIZE)) != 0)) {
        if (res < 0) {
            fprintf(stderr, "Fatal read error: %s\n", strerror(errno));
            return ZX_ERR_IO;
        }
        total_read += res;

        ssize_t buf_len = res;
        ssize_t total_written = 0;
        while (total_written < buf_len) {
            ssize_t res = write(dest_fd.get(), buf.get() + total_written, buf_len - total_written);
            if (res < 0) {
                fprintf(stderr, "Fatal write err: %s\n", strerror(errno));
                return ZX_ERR_IO;
            }
            total_written += res;
        }
    }
    return ZX_OK;
}

int main(int argc, char** argv) {
    auto print_usage = fbl::MakeAutoCall([argv]() { usage(argv[0]); });

    const char* filename = nullptr;
    uint32_t stream_id = DEFAULT_STREAM_ID;
    bool download = true;

    int opt;
    while ((opt = getopt(argc, argv, "i:f:du")) != -1) {
        switch (opt) {
        case 'i':
            if (sscanf(optarg, "%u", &stream_id) != 1) {
                fprintf(stderr, "Failed to parse stream id: \"%s\"\n", optarg);
                return -1;
            }
            if (stream_id == 0) {
                fprintf(stderr, "Stream ID must be positive\n");
                return -1;
            }
            break;
        case 'f':
            filename = optarg;
            break;
        case 'd':
            download = true;
            break;
        case 'u':
            download = false;
            break;
        default:
            fprintf(stderr, "Invalid option\n");
            return -1;
        }
    }
    if (!filename) {
        fprintf(stderr, "No file specified\n");
        return -1;
    }
    // Finished parsing the arguments without error.
    print_usage.cancel();

    fbl::unique_fd xdc_fd;
    zx_status_t status = configure_xdc(stream_id, xdc_fd);
    if (status != ZX_OK) {
        return -1;
    }

    int file_flags = download ? (O_RDWR | O_CREAT) : O_RDONLY;
    fbl::unique_fd file_fd(open(filename, file_flags, 0666));
    if (!file_fd) {
        fprintf(stderr, "Failed to open \"%s\", err %s\n", filename, strerror(errno));
        return -1;
    }

    fbl::unique_fd src_fd;
    fbl::unique_fd dest_fd;

    file_header_t file_header;
    if (download) {
        if (read_file_header(xdc_fd, &file_header) != ZX_OK) {
            return -1;
        }
        src_fd = fbl::move(xdc_fd);
        dest_fd = fbl::move(file_fd);
    } else {
        if (write_file_header(file_fd, xdc_fd, &file_header) != ZX_OK) {
            return -1;
        }
        src_fd = fbl::move(file_fd);
        dest_fd = fbl::move(xdc_fd);
    }

    status = transfer(src_fd, file_header.file_size, dest_fd);
    if (status != ZX_OK) {
        return -1;
    }
    return 0;
}
