// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_NAMESPACE_H_
#define SRC_DEVICES_LIB_DRIVER2_NAMESPACE_H_

#include <fuchsia/component/runner/llcpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/status.h>

// Manages a driver's namespace.
class Namespace {
 public:
  // Creates a namespace from `DriverStartArgs::ns`.
  static zx::status<Namespace> Create(
      fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry>& entries);

  Namespace() = default;
  ~Namespace();

  Namespace(Namespace&& other) noexcept;
  Namespace& operator=(Namespace&& other) noexcept;

  // Connect to a service within a driver's namespace.
  zx::status<zx::channel> Connect(std::string_view path) const;

 private:
  explicit Namespace(fdio_ns_t* ns);

  Namespace(const Namespace& other) = delete;
  Namespace& operator=(const Namespace& other) = delete;

  fdio_ns_t* ns_ = nullptr;
};

#endif  // SRC_DEVICES_LIB_DRIVER2_NAMESPACE_H_
