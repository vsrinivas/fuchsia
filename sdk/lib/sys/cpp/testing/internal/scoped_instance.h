// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_INTERNAL_SCOPED_INSTANCE_H_
#define LIB_SYS_CPP_TESTING_INTERNAL_SCOPED_INSTANCE_H_

// #include <fuchsia/io2/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
// #include <zircon/system/public/zircon/types.h>

// #include <memory>
// #include <string>
// #include <utility>
// #include <variant>

#include <src/lib/fxl/macros.h>

namespace sys::testing::internal {

// A scoped instance of a dynamically created child component. This class
// will automatically destroy the child component once it goes out of scope.
class ScopedInstance {
 public:
  // Same as below with a randomly generated `name`.
  static ScopedInstance New(const sys::ComponentContext* context, std::string collection,
                            std::string url);

  // Create a dynamic child component using the fuchsia.sys2.Realm API.
  // |context| must not be NULL.
  // |collection| is the name of the collection to create the child under. This
  // field must refer to a name in the current component's manifest file.
  // |name| is the name to assign to the child.
  // |url| is the component component URL of the child component.
  static ScopedInstance New(const sys::ComponentContext* context, std::string collection,
                            std::string name, std::string url);

  ~ScopedInstance();

  ScopedInstance(ScopedInstance&&) noexcept;
  ScopedInstance& operator=(ScopedInstance&&) noexcept;

  FXL_DISALLOW_COPY_AND_ASSIGN(ScopedInstance);

  // Connect to exposed directory of the child component.
  template <typename Interface>
  zx_status_t ConnectAtExposedDir(fidl::InterfaceRequest<Interface> request) const {
    return exposed_dir_.Connect<Interface>(std::move(request));
  }

  // Get the child name of this instance.
  std::string GetChildName() const;

 private:
  explicit ScopedInstance(fuchsia::sys2::RealmSyncPtr, fuchsia::sys2::ChildRef child_ref_,
                          ServiceDirectory exposed_dir);

  fuchsia::sys2::RealmSyncPtr realm_proxy_;
  fuchsia::sys2::ChildRef child_ref_;
  ServiceDirectory exposed_dir_;
  bool has_moved_;
};

}  // namespace sys::testing::internal

#endif  // LIB_SYS_CPP_TESTING_INTERNAL_SCOPED_INSTANCE_H_
