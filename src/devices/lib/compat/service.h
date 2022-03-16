// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_COMPAT_SERVICE_H_
#define SRC_DEVICES_LIB_COMPAT_SERVICE_H_

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

  static zx::status<OwnedInstance> Create(std::string_view service_name,
                                          fbl::RefPtr<fs::PseudoDir> service, std::string_view name,
                                          fbl::RefPtr<fs::PseudoDir> instance) {
    zx_status_t status = service->AddEntry(name, instance);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    return zx::ok(OwnedInstance(service_name, std::move(service), name, std::move(instance)));
  }

  std::string_view service_name() { return service_name_; }
  std::string_view instance_name() { return name_; }

 private:
  OwnedInstance(std::string_view service_name, fbl::RefPtr<fs::PseudoDir> service,
                std::string_view name, fbl::RefPtr<fs::PseudoDir> instance)
      : service_name_(service_name),
        name_(name),
        service_(std::move(service)),
        instance_(std::move(instance)) {}
  std::string service_name_;
  std::string name_;
  fbl::RefPtr<fs::PseudoDir> service_;
  fbl::RefPtr<fs::PseudoDir> instance_;
};

// An OwnedProtocol is a class that owns a protocol. When this
// class goes out of scope, the protocol will be removed from its parent directory.
class OwnedProtocol {
 public:
  // This class can only be moved, it cannot be copied.
  OwnedProtocol(const OwnedProtocol&) = delete;
  OwnedProtocol& operator=(const OwnedProtocol&) = delete;
  OwnedProtocol(OwnedProtocol&& other) = default;
  OwnedProtocol& operator=(OwnedProtocol&& other) = default;

  ~OwnedProtocol() {
    if (parent_) {
      parent_->RemoveEntry(name_);
    }
  }

  static zx::status<OwnedProtocol> Create(fbl::RefPtr<fs::PseudoDir> parent, std::string_view name,
                                          fbl::RefPtr<fs::Vnode> protocol) {
    zx_status_t status = parent->AddEntry(name, protocol);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    return zx::ok(OwnedProtocol(std::move(parent), name, std::move(protocol)));
  }

 private:
  OwnedProtocol(fbl::RefPtr<fs::PseudoDir> parent, std::string_view name,
                fbl::RefPtr<fs::Vnode> protocol)
      : name_(name), parent_(std::move(parent)), protocol_(std::move(protocol)) {}
  std::string name_;
  fbl::RefPtr<fs::PseudoDir> parent_;
  fbl::RefPtr<fs::Vnode> protocol_;
};

}  // namespace compat

#endif  // SRC_DEVICES_LIB_COMPAT_SERVICE_H_
