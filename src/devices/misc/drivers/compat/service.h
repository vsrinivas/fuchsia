// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_SERVICE_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_SERVICE_H_

#include <string>

#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

namespace compat {

// An OwnedInstance is a class that owns an instance in a service. When this
// class goes out of scope, the instance will be removed from the service.
class OwnedInstance {
 public:
  // This class can only be moved, it cannot be copied.
  OwnedInstance(const OwnedInstance&) = delete;
  OwnedInstance& operator=(const OwnedInstance&) = delete;
  OwnedInstance(OwnedInstance&& other) = default;
  OwnedInstance& operator=(OwnedInstance&& other) = default;

  ~OwnedInstance() {
    if (service_) {
      service_->RemoveEntry(name_);
    }
  }

  static zx::status<OwnedInstance> Create(fbl::RefPtr<fs::PseudoDir> service, std::string_view name,
                                          fbl::RefPtr<fs::PseudoDir> instance) {
    zx_status_t status = service->AddEntry(name, instance);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    return zx::ok(OwnedInstance(std::move(service), name, std::move(instance)));
  }

 private:
  OwnedInstance(fbl::RefPtr<fs::PseudoDir> service, std::string_view name,
                fbl::RefPtr<fs::PseudoDir> instance)
      : name_(name), service_(std::move(service)), instance_(std::move(instance)) {}
  std::string name_;
  fbl::RefPtr<fs::PseudoDir> service_;
  fbl::RefPtr<fs::PseudoDir> instance_;
};

}  // namespace compat

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_SERVICE_H_
