// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_
#define SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_

#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/status.h>

#include <fbl/intrusive_double_list.h>
#include <fs/pseudo_dir.h>

#include "src/devices/lib/driver2/record.h"

class Driver : public llcpp::fuchsia::driver::framework::Driver::Interface,
               public fbl::DoublyLinkedListable<std::unique_ptr<Driver>> {
 public:
  static zx::status<std::unique_ptr<Driver>> Load(zx::vmo vmo);

  Driver(void* library, DriverRecordV1* record);
  ~Driver();

  void set_binding(fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Driver> binding);

  zx::status<> Start(const fidl::OutgoingMessage& message, async_dispatcher_t* dispatcher);

 private:
  void* library_;
  DriverRecordV1* record_;
  void* opaque_ = nullptr;
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::driver::framework::Driver>> binding_;
};

class DriverHost : public llcpp::fuchsia::driver::framework::DriverHost::Interface {
 public:
  // DriverHost does not take ownership of |loop|.
  explicit DriverHost(async::Loop* loop);

  zx::status<> PublishDriverHost(const fbl::RefPtr<fs::PseudoDir>& svc_dir);

 private:
  void Start(llcpp::fuchsia::driver::framework::DriverStartArgs start_args, zx::channel request,
             StartCompleter::Sync& completer) override;

  async::Loop* loop_;
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> drivers_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_
