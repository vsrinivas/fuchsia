// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_COMPONENT_CPP_DRIVER_CONTEXT_H_
#define LIB_DRIVER_COMPONENT_CPP_DRIVER_CONTEXT_H_

#include <lib/driver/component/cpp/namespace.h>
#include <lib/driver/component/cpp/outgoing_directory.h>

namespace driver {

// |DriverContext| is meant to be used in drivers for easy access to the
// incoming and outgoing namespace. The interface is very similar to |ComponentContext|.
// See more information at `sdk/lib/sys/cpp/component_context.h`.
class DriverContext final {
 public:
  explicit DriverContext(fdf_dispatcher_t* dispatcher);

  ~DriverContext();

  DriverContext(const DriverContext&) = delete;
  DriverContext& operator=(const DriverContext&) = delete;

  void InitializeAndServe(Namespace incoming,
                          fidl::ServerEnd<fuchsia_io::Directory> outgoing_directory_request);

  // Used to access the incoming namespace of the driver. Similar to |svc| from |ComponentContext|.
  const std::shared_ptr<Namespace>& incoming() const { return incoming_; }
  fidl::UnownedClientEnd<fuchsia_io::Directory> svc() const { return incoming_->svc_dir(); }

  // Used to access the outgoing directory that the driver is serving. Similar to |outgoing|
  // from |ComponentContext|.
  std::shared_ptr<OutgoingDirectory>& outgoing() { return outgoing_; }
  const std::shared_ptr<OutgoingDirectory>& outgoing() const { return outgoing_; }

 private:
  fdf_dispatcher_t* dispatcher_;
  std::shared_ptr<Namespace> incoming_;
  std::shared_ptr<OutgoingDirectory> outgoing_;
};

}  // namespace driver

#endif  // LIB_DRIVER_COMPONENT_CPP_DRIVER_CONTEXT_H_
