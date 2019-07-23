// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/vfs/cpp/composed_service_dir.h>
#include <zircon/status.h>

namespace vfs {

ComposedServiceDir::ComposedServiceDir() : root_(std::make_unique<vfs::PseudoDir>()) {}

ComposedServiceDir::~ComposedServiceDir() {}

void ComposedServiceDir::set_fallback(fidl::InterfaceHandle<fuchsia::io::Directory> fallback_dir) {
  fallback_dir_ = fallback_dir.TakeChannel();
}

void ComposedServiceDir::AddService(const std::string& service_name,
                                    std::unique_ptr<vfs::Service> service) {
  root_->AddEntry(service_name, std::move(service));
}

zx_status_t ComposedServiceDir::GetAttr(fuchsia::io::NodeAttributes* out_attributes) const {
  return root_->GetAttr(out_attributes);
}

zx_status_t ComposedServiceDir::Readdir(uint64_t offset, void* data, uint64_t len,
                                        uint64_t* out_offset, uint64_t* out_actual) {
  return root_->Readdir(offset, data, len, out_offset, out_actual);
}

zx_status_t ComposedServiceDir::Lookup(const std::string& name, vfs::internal::Node** out) const {
  zx_status_t status = root_->Lookup(name, out);
  if (status == ZX_OK) {
    return status;
  }
  if (fallback_dir_) {
    auto entry = fallback_services_.find(name);
    if (entry != fallback_services_.end()) {
      *out = entry->second.get();
    } else {
      auto service = std::make_unique<vfs::Service>(
          [name = std::string(name.data(), name.length()), dir = &fallback_dir_](
              zx::channel request, async_dispatcher_t* dispatcher) {
            fdio_service_connect_at(dir->get(), name.c_str(), request.release());
          });
      *out = service.get();
      fallback_services_[name] = std::move(service);
    }
  } else {
    return ZX_ERR_NOT_FOUND;
  }
  return ZX_OK;
}

}  // namespace vfs
