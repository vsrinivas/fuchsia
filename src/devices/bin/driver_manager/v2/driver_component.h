// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DRIVER_COMPONENT_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DRIVER_COMPONENT_H_

#include <fidl/fuchsia.component.runner/cpp/wire.h>
#include <fidl/fuchsia.driver.host/cpp/wire.h>

namespace dfv2 {
class DriverComponent : public fidl::WireServer<fuchsia_component_runner::ComponentController>,
                        public fidl::WireAsyncEventHandler<fuchsia_driver_host::Driver> {
 public:
  // The driver will call this function when it would like to be removed.
  // This function should shut down all of the children of the driver.
  using RequestRemoveCallback = fit::function<void(zx_status_t)>;

  // The driver will call this function when it has lost connection to the
  // driver_host/driver component. The driver is dead and must be removed.
  using RemoveCallback = fit::function<void(zx_status_t)>;

  explicit DriverComponent(fidl::ClientEnd<fuchsia_driver_host::Driver> driver,
                           fidl::ServerEnd<fuchsia_component_runner::ComponentController> component,
                           async_dispatcher_t* dispatcher, std::string_view url,
                           RequestRemoveCallback request_remove, RemoveCallback remove);

  // This is true when the class is connected to the underlying driver component.
  // If the driver host or driver component connection is removed, this will
  // be false.
  inline bool is_alive() const { return is_alive_; }

  inline std::string_view url() const { return url_; }

  // Request that this Driver be stopped. This will go through and
  // stop all of the Driver's children first.
  void RequestDriverStop();

  // Signal to the DriverHost that this Driver should be stopped.
  // This function should only be called after all of this Driver's children
  // have been stopped.
  // This should only be used by the Node class.
  zx_status_t StopDriver();

 private:
  // This is called when fuchsia_driver_framework::Driver is closed.
  void on_fidl_error(fidl::UnbindInfo error) override;

  // fidl::WireServer<fuchsia_component_runner::ComponentController>
  void Stop(StopCompleter::Sync& completer) override;
  void Kill(KillCompleter::Sync& completer) override;

  // Close the component connection to signal to CF that the component has stopped.
  // Once the component connection is closed, this class will eventually be
  // freed.
  void StopComponent();

  bool stop_in_progress_ = false;
  bool is_alive_ = true;

  // This channel represents the Driver in the DriverHost. If we call
  // Stop() on this channel, the DriverHost will call Stop on the Driver
  // and drop its end of the channel when it is finished.
  // When the other end of this channel is dropped, DriverComponent will
  // signal to ComponentFramework that the component has stopped.
  fidl::WireSharedClient<fuchsia_driver_host::Driver> driver_;

  // This represents the Driver Component within the Component Framework.
  // When this is closed with an epitaph it signals to the Component Framework
  // that this driver component has stopped.
  std::optional<fidl::ServerBindingRef<fuchsia_component_runner::ComponentController>> driver_ref_;

  // URL of the driver's component manifest
  std::string url_;

  RequestRemoveCallback request_remove_;
  RemoveCallback remove_;
};

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DRIVER_COMPONENT_H_
