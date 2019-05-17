// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>

#include <sys/types.h>
#include <zircon/types.h>

int netcp_open(const char* filename, uint32_t arg, size_t* file_size);

ssize_t netcp_offset_read(void* data_out, off_t offset, size_t max_len);

ssize_t netcp_read(void* data_out, size_t data_sz);

ssize_t netcp_offset_write(const char* data, off_t offset, size_t length);

ssize_t netcp_write(const char* data, size_t len);

int netcp_close();

void netcp_abort_write();


namespace netsvc {

class NetCopyInterface {
public:
    virtual ~NetCopyInterface() {}
    virtual int Open(const char* filename, uint32_t arg, size_t* file_size) = 0;
    virtual ssize_t Read(void* data_out, std::optional<off_t> offset, size_t max_len) = 0;
    virtual ssize_t Write(const char* data, std::optional<off_t> offset, size_t length) = 0;
    virtual int Close() = 0;
    virtual void AbortWrite() = 0;
};

class NetCopy : public NetCopyInterface {
public:
    explicit NetCopy() {}

    int Open(const char* filename, uint32_t arg, size_t* file_size) final {
        return netcp_open(filename, arg, file_size);
    }

    ssize_t Read(void* data_out, std::optional<off_t> offset, size_t max_len) final {
        if (offset) {
            return netcp_offset_read(data_out, *offset, max_len);
        } else {
            return netcp_read(data_out, max_len);
        }
    }

    ssize_t Write(const char* data, std::optional<off_t> offset, size_t length) final {
        if (offset) {
            return netcp_offset_write(data, *offset, length);
        } else {
            return netcp_write(data, length);
        }
    }

    int Close() final {
        return netcp_close();
    }

    void AbortWrite() final {
        netcp_abort_write();
    }
};

} // namespace netsvc
