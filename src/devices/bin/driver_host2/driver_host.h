// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_
#define SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_

#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <lib/zx/status.h>

#include <fbl/intrusive_double_list.h>
#include <fs/pseudo_dir.h>

#include "src/devices/lib/driver2/record.h"

class Driver : public llcpp::fuchsia::driver::framework::Driver::Interface,
               public fbl::DoublyLinkedListable<std::unique_ptr<Driver>> {
 public:
  explicit Driver(zx::vmo vmo);
  ~Driver();

  bool ok() const;
  void set_binding(fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Driver> binding);

  zx::status<> Start(fidl_msg_t* msg, async_dispatcher_t* dispatcher);

 private:
  void* library_;
  DriverRecordV1* record_;
  void* opaque_;
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Driver>> binding_;
};

class DriverHost : public llcpp::fuchsia::driver::framework::DriverHost::Interface {
 public:
  explicit DriverHost(async_dispatcher_t* dispatcher);

  zx::status<> PublishDriverHost(const fbl::RefPtr<fs::PseudoDir>& svc_dir);

 private:
  void Start(llcpp::fuchsia::driver::framework::DriverStartArgs start_args, zx::channel request,
             StartCompleter::Sync& completer) override;

  async_dispatcher_t* dispatcher_;
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> drivers_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_
