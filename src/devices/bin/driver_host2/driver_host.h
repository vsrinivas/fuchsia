// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_
#define SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>
#include <lib/zx/status.h>
#include <zircon/compiler.h>

#include <fbl/intrusive_double_list.h>

#include "src/devices/lib/driver2/record.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

class Driver : public fidl::WireServer<fuchsia_driver_framework::Driver>,
               public fbl::DoublyLinkedListable<std::unique_ptr<Driver>> {
 public:
  static zx::status<std::unique_ptr<Driver>> Load(std::string url, zx::vmo vmo);

  Driver(std::string url, void* library, const DriverRecordV1* record);
  ~Driver();

  const std::string& url() const { return url_; }
  void set_binding(fidl::ServerBindingRef<fuchsia_driver_framework::Driver> binding);

  void Stop(StopRequestView request, StopCompleter::Sync& completer) override;

  // Starts the driver.
  //
  // The handles in `message` will be consumed.
  zx::status<> Start(fidl::IncomingMessage& message, async_dispatcher_t* driver_dispatcher);

 private:
  std::string url_;
  void* library_;
  const DriverRecordV1* record_;
  std::optional<void*> opaque_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_framework::Driver>> binding_;
};

class DriverHost : public fidl::WireServer<fuchsia_driver_framework::DriverHost> {
 public:
  // DriverHost does not take ownership of `loop`, and `loop` must outlive
  // DriverHost.
  DriverHost(inspect::Inspector& inspector, async::Loop& loop,
             async_dispatcher_t* driver_dispatcher);

  fpromise::promise<inspect::Inspector> Inspect();
  zx::status<> PublishDriverHost(component::OutgoingDirectory& outgoing_directory);

 private:
  // fidl::WireServer<fuchsia_driver_framework::DriverHost>
  void Start(StartRequestView request, StartCompleter::Sync& completer) override;

  async::Loop& loop_;
  async_dispatcher_t* driver_dispatcher_;
  std::mutex mutex_;
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> drivers_ __TA_GUARDED(mutex_);
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_
