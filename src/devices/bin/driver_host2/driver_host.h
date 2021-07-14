// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_
#define SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_

#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/status.h>
#include <zircon/compiler.h>

#include <fbl/intrusive_double_list.h>

#include "src/devices/lib/driver2/record.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"

class Driver : public fidl::WireServer<fuchsia_driver_framework::Driver>,
               public fbl::DoublyLinkedListable<std::unique_ptr<Driver>> {
 public:
  static zx::status<std::unique_ptr<Driver>> Load(std::string url, std::string binary, zx::vmo vmo);

  Driver(std::string url, std::string binary, void* library, DriverRecordV1* record);
  ~Driver();

  const std::string& url() const { return url_; }
  const std::string& binary() const { return binary_; }
  void set_binding(fidl::ServerBindingRef<fuchsia_driver_framework::Driver> binding);

  // Starts the driver.
  //
  // The handles in `message` will be consumed.
  zx::status<> Start(fidl::IncomingMessage& message, async_dispatcher_t* driver_dispatcher);

 private:
  std::string url_;
  std::string binary_;
  void* library_;
  DriverRecordV1* record_;
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
  zx::status<> PublishDriverHost(const fbl::RefPtr<fs::PseudoDir>& svc_dir);

 private:
  // fidl::WireServer<fuchsia_driver_framework::DriverHost>
  void Start(StartRequestView request, StartCompleter::Sync& completer) override;

  async::Loop& loop_;
  async_dispatcher_t* driver_dispatcher_;
  std::mutex mutex_;
  fbl::DoublyLinkedList<std::unique_ptr<Driver>> drivers_ __TA_GUARDED(mutex_);
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_HOST_H_
