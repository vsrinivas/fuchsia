// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_H_
#define SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_H_

#include <fidl/fuchsia.driver.host/cpp/fidl.h>
#include <lib/driver2/record.h>
#include <lib/fdf/cpp/dispatcher.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace dfv2 {

class Driver : public fidl::Server<fuchsia_driver_host::Driver>,
               public fbl::RefCounted<Driver>,
               public fbl::DoublyLinkedListable<fbl::RefPtr<Driver>> {
 public:
  static zx::result<fbl::RefPtr<Driver>> Load(std::string url, zx::vmo vmo);

  Driver(std::string url, void* library, const DriverRecordV1* record);
  ~Driver() override;

  const std::string& url() const { return url_; }
  void set_binding(fidl::ServerBindingRef<fuchsia_driver_host::Driver> binding);

  void Stop(StopCompleter::Sync& completer) override;

  // Starts the driver.
  zx::result<> Start(fuchsia_driver_framework::DriverStartArgs start_args,
                     fdf::Dispatcher dispatcher);

 private:
  std::string url_;
  void* library_;
  const DriverRecordV1* record_;
  std::optional<void*> opaque_;
  std::optional<fidl::ServerBindingRef<fuchsia_driver_host::Driver>> binding_;

  // The initial dispatcher passed to the driver.
  // This must be shutdown by before this driver object is destructed.
  fdf::Dispatcher initial_dispatcher_;
};

// Extracts the default_dispatcher_opts from |program| and converts it to
// the options value expected by |fdf::Dispatcher::Create|.
// Returns zero if no options were specified.
uint32_t ExtractDefaultDispatcherOpts(const fuchsia_data::wire::Dictionary& program);

zx::result<fdf::Dispatcher> CreateDispatcher(fbl::RefPtr<Driver> driver, uint32_t dispatcher_opts);

struct LoadedDriver {
  fbl::RefPtr<Driver> driver;
  fuchsia_driver_framework::DriverStartArgs start_args;
  fdf::Dispatcher dispatcher;
};

void LoadDriver(fuchsia_driver_framework::DriverStartArgs start_args,
                async_dispatcher_t* dispatcher,
                fit::callback<void(zx::result<LoadedDriver>)> callback);

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_HOST2_DRIVER_H_
