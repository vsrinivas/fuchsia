// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <fvm/sparse-reader.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

namespace paver {

// Implements fvm::ReaderInterface to allow interoperability between paver and
// fvm sparse reader library.
class StreamReader : public fvm::ReaderInterface {
public:
    static zx_status_t Create(zx::channel stream, fbl::unique_ptr<StreamReader>* reader);

    virtual ~StreamReader() = default;

    virtual zx_status_t Read(void* buf, size_t buf_size, size_t* size_actual) final;

private:
    StreamReader(zx::channel stream, zx::vmo vmo)
        : stream_(std::move(stream)), vmo_(std::move(vmo)) {}

    StreamReader(const StreamReader&) = delete;
    StreamReader& operator=(const StreamReader&) = delete;
    StreamReader(StreamReader&&) = delete;
    StreamReader& operator=(StreamReader&&) = delete;

    zx::channel stream_;
    zx::vmo vmo_;
    zx_off_t offset_ = 0;
    size_t size_ = 0;
};

} // namespace paver
