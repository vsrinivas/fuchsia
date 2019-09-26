// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DRIVER_FRAMEWORK_DEVCOORDINATOR_VMO_WRITER_H_
#define SRC_DRIVER_FRAMEWORK_DEVCOORDINATOR_VMO_WRITER_H_

#include <lib/zx/vmo.h>

#include <utility>

namespace devmgr {

// Wraps a vmo to aid in writing text into it.
class VmoWriter {
 public:
  explicit VmoWriter(zx::vmo vmo) : vmo_(std::move(vmo)) { status_ = vmo_.get_size(&size_); }

  // printf into the vmo.
  void Printf(const char* fmt, ...) __PRINTFLIKE(2, 3);

  size_t written() const { return written_; }
  size_t available() const { return written_; }
  const zx::vmo& vmo() const { return vmo_; }
  zx_status_t status() const { return status_; }

 private:
  zx::vmo vmo_;
  size_t size_ = 0;
  zx_status_t status_ = ZX_OK;
  size_t written_ = 0;
  size_t available_ = 0;
};

}  // namespace devmgr

#endif  // SRC_DRIVER_FRAMEWORK_DEVCOORDINATOR_VMO_WRITER_H_
