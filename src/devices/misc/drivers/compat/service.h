// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_SERVICE_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_SERVICE_H_

#include <string>

#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace compat {

// A ServiceDir represents DFv2's version of a service.
// This is a helper class which will remove the directory from its parent
// when this class goes out of scope.
class ServiceDir {
 public:
  // This class can only be moved, it cannot be copied.
  ServiceDir(const ServiceDir&) = delete;
  ServiceDir& operator=(const ServiceDir&) = delete;
  ServiceDir(ServiceDir&& other) = default;
  ServiceDir& operator=(ServiceDir&& other) = default;

  ~ServiceDir() {
    if (parent_) {
      parent_->RemoveEntry(name_);
    }
  }

  static zx::status<ServiceDir> Create(fbl::RefPtr<fs::PseudoDir> parent, std::string_view name) {
    ServiceDir service(std::move(parent));

    service.dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
    service.name_ = name;

    zx_status_t status = service.parent_->AddEntry(name, service.dir_);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    return zx::ok(std::move(service));
  }

  fbl::RefPtr<fs::PseudoDir>& dir() { return dir_; }

 private:
  explicit ServiceDir(fbl::RefPtr<fs::PseudoDir> parent) : parent_(std::move(parent)) {}

  std::string name_;
  fbl::RefPtr<fs::PseudoDir> parent_;
  fbl::RefPtr<fs::PseudoDir> dir_;
};

}  // namespace compat

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_SERVICE_H_
