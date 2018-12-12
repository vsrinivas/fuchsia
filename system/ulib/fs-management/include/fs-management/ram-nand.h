// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <inttypes.h>

#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <fbl/unique_fd.h>
#include <zircon/compiler.h>
#include <zircon/nand/c/fidl.h>

namespace fs_mgmt {

class RamNand {
public:
    // Creates a ram_nand.
    static zx_status_t Create(const zircon_nand_RamNandInfo* config, std::unique_ptr<RamNand>* out);

    // Not copyable.
    RamNand(RamNand&) = delete;
    RamNand& operator=(RamNand&) = delete;

    // Movable.
    RamNand(RamNand&&) = default;
    RamNand& operator=(RamNand&&) = default;

    ~RamNand();

    // Don't unbind in destructor.
    void NoUnbind() { unbind = false; }

    int fd() { return fd_.get(); }
    const char* path() { return path_.c_str(); }

private:
    RamNand(fbl::StringPiece path, fbl::unique_fd fd)
        : path_(std::move(path)), fd_(std::move(fd)) {}

    fbl::String path_;
    fbl::unique_fd fd_;
    bool unbind = true;
};

} // namespace fs_mgmt
