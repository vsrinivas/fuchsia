// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <tftp/tftp.h>
#include <zircon/boot/netboot.h>

#include "netcp.h"
#include "paver.h"

namespace netsvc {

// Provides capabilities to read/write files sent over TFTP.
//
// Reads only implements netcp. Specifically it enables reading of files in
// global /data.
//
// Writes come in 4 flavors:
// * netcp: Ability to write to global /data.
// * netboot: Mexec into image once write completes.
// * paving: Writes boot partions, or FVM.
// * board name validation: Validates that board name sent matches current
//   board.
class FileApiInterface {
public:
    // Returns size of file on success.
    virtual ssize_t OpenRead(const char* filename) = 0;
    virtual tftp_status OpenWrite(const char* filename, size_t size) = 0;
    virtual tftp_status Read(void* data, size_t* length, off_t offset) = 0;
    virtual tftp_status Write(const void* data, size_t* length, off_t offset) = 0;
    virtual void Close() = 0;
    // Like close, but signals read or write operation was incomplete.
    virtual void Abort() = 0;

    virtual bool is_write() = 0;
    virtual const char* filename() = 0;
};

class FileApi : public FileApiInterface {
public:
    // FileApi does *not* take ownership of |paver|.
    explicit FileApi(bool is_zedboot,
                     std::unique_ptr<NetCopyInterface> netcp = std::make_unique<NetCopy>(),
                     zx::channel sysinfo = zx::channel(),
                     PaverInterface* paver = Paver::Get());

    ssize_t OpenRead(const char* filename) final;
    tftp_status OpenWrite(const char* filename, size_t size) final;
    tftp_status Read(void* data, size_t* length, off_t offset) final;
    tftp_status Write(const void* data, size_t* length, off_t offset) final;
    void Close() final;
    void Abort() final;

    const char* filename() final {
        return filename_;
    }

    bool is_write() final {
        return is_write_;
    }

private:
    // Identifies what the file being streamed over TFTP should be
    // used for.
    enum class NetfileType {
        kUnknown,   // No reads/writes currently in progress.
        kNetCopy,   // A file in /data
        kNetboot,   // A bootfs file
        kPaver,     // A disk image which should be paved to disk
        kBoardName, // A file containing the board name.
                    // Expected to return error if it doesn't match the current board name.
    };

    bool is_zedboot_;

    bool is_write_ = false;
    char filename_[PATH_MAX + 1] = {};
    NetfileType type_ = NetfileType::kUnknown;

    // Use when type_ == NetfileType::kBoardName.
    zx::channel sysinfo_;

    // Used when type_ == NetfileType::kNetCopy.
    std::unique_ptr<NetCopyInterface> netcp_;

    // Only valid when type_ == NetfileType::kNetboot.
    nbfile* netboot_file_ = nullptr;

    // Used when type_ == NetfileType::kPaver.
    PaverInterface* paver_;
};

} // namespace netsvc
