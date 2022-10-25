// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_HARDWARE_DISPLAY_CONTROLLER_PROVIDER_FAKE_SERVICE_H_
#define SRC_UI_BIN_HARDWARE_DISPLAY_CONTROLLER_PROVIDER_FAKE_SERVICE_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <memory>
#include <queue>

#include "src/graphics/display/drivers/fake/fake-display-device-tree.h"
#include "src/lib/fxl/macros.h"

namespace sys {
class ComponentContext;
}  // namespace sys

namespace fake_display {

// Not thread-safe.  The assumption is that the public methods will be invoked by FIDL bindings
// on a single-threaded event-loop.
class ProviderService : public fuchsia::hardware::display::Provider {
 public:
  // |app_context| is used to publish this service.
  ProviderService(sys::ComponentContext* app_context, async_dispatcher_t* dispatcher);
  ~ProviderService();

  // |fuchsia::hardware::display::Provider|.
  void OpenVirtconController(
      ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller_request,
      OpenVirtconControllerCallback callback) override;

  // |fuchsia::hardware::display::Provider|.
  void OpenController(
      ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller_request,
      OpenControllerCallback callback) override;

  // For tests.
  size_t num_queued_requests() const { return state_->queued_requests.size(); }
  size_t num_virtcon_queued_requests() const { return state_->virtcon_queued_requests.size(); }
  bool controller_claimed() const { return state_->controller_claimed; }
  bool virtcon_controller_claimed() const { return state_->virtcon_controller_claimed; }

 private:
  struct Request {
    bool is_virtcon;
    zx::channel device;
    ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller_request;
    OpenControllerCallback callback;
  };

  // Encapsulates state for thread safety, since |display::FakeDisplayDeviceTree| invokes callbacks
  // from other threads.
  struct State {
    async_dispatcher_t* const dispatcher;

    std::unique_ptr<display::FakeDisplayDeviceTree> tree;

    bool controller_claimed = false;
    bool virtcon_controller_claimed = false;
    std::queue<Request> queued_requests;
    std::queue<Request> virtcon_queued_requests;
  };

  // Called by OpenVirtconController() and OpenController().
  void ConnectOrDeferClient(Request request);

  // Must be called from main dispatcher thread.
  static void ConnectClient(Request request, const std::shared_ptr<State>& state);

  std::shared_ptr<State> state_;

  fidl::BindingSet<fuchsia::hardware::display::Provider> bindings_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ProviderService);
};

}  // namespace fake_display

#endif  // SRC_UI_BIN_HARDWARE_DISPLAY_CONTROLLER_PROVIDER_FAKE_SERVICE_H_
