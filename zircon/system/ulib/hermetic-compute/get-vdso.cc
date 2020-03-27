// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <filesystem>
#include <lib/fdio/io.h>
#include <lib/hermetic-compute/hermetic-compute.h>
#include <lib/zx/vmo.h>
#include <map>
#include <mutex>

namespace {

// TODO(mcgrathr): Maybe default to most-restricted one?
constexpr const char* kDefaultVdso = "full";

}  // namespace

const zx::vmo& HermeticComputeProcess::GetVdso(const char* variant) {
  if (!variant) {
    variant = kDefaultVdso;
  }

  static std::map<std::string, zx::vmo> table;
  static std::mutex lock;
  std::lock_guard<std::mutex> guard(lock);
  zx::vmo& vmo = table[variant];

  if (!vmo.is_valid()) {
    // TODO(mcgrathr): maybe there should be a special ldsvc instance
    // somewhere that vends vDSO VMOs?
    std::filesystem::path filename("/boot/kernel/vdso");
    filename /= variant;
    fbl::unique_fd fd(open(filename.c_str(), O_RDONLY));
    if (fd) {
      zx_status_t status = fdio_get_vmo_exact(fd.get(), vmo.reset_and_get_address());
      if (status == ZX_OK) {
        status = vmo.replace_as_executable(zx::resource(), &vmo);
      }
      if (status != ZX_OK) {
        vmo.reset();
      }
    }
  }

  return vmo;
}
