// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VFS_CPP_COMPOSED_SERVICE_DIR_H_
#define LIB_VFS_CPP_COMPOSED_SERVICE_DIR_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>

#include <map>
#include <string>

namespace vfs {

// A directory-like object which created a composed PseudoDir on top of
// |fallback_dir|.It can be used to connect to services in |fallback_dir| but it
// will not enumerate them.
class ComposedServiceDir : public vfs::internal::Directory {
 public:
  ComposedServiceDir();
  ~ComposedServiceDir() override;

  void set_fallback(fidl::InterfaceHandle<fuchsia::io::Directory> fallback_dir);

  void AddService(const std::string& service_name,
                  std::unique_ptr<vfs::Service> service);

  //
  // |vfs::internal::Node| Implementations:
  //
  zx_status_t Lookup(const std::string& name,
                     vfs::internal::Node** out_node) const final;

  zx_status_t GetAttr(fuchsia::io::NodeAttributes* out_attributes) const final;

  zx_status_t Readdir(uint64_t offset, void* data, uint64_t len,
                      uint64_t* out_offset, uint64_t* out_actual) final;

 private:
  std::unique_ptr<vfs::PseudoDir> root_;
  zx::channel fallback_dir_;
  // The collection of services that have been looked up on the fallback
  // directory. These services are just passthrough in the sense that they
  // forward connection requests to the fallback directory. Since there is no
  // good way in the present context to know whether these service entries
  // actually match an existing service, and since the present object must own
  // these entries, we keep them around until the present object gets deleted.
  mutable std::map<std::string, std::unique_ptr<vfs::Service>>
      fallback_services_;

  // Disallow copy and assignment.
  ComposedServiceDir(const ComposedServiceDir&) = delete;
  ComposedServiceDir& operator=(const ComposedServiceDir&) = delete;
};

}  // namespace vfs

#endif  // LIB_VFS_CPP_COMPOSED_SERVICE_DIR_H_
