// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_BUTTONS_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_BUTTONS_H_

#include <fuchsia/buttons/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include <ddktl/protocol/buttons.h>

class ButtonsImpl : public fuchsia::buttons::Buttons {
 public:
  ButtonsImpl(fidl::InterfaceRequest<fuchsia::buttons::Buttons> buttons_interface,
              async_dispatcher_t* dispatcher, fit::closure on_connection_closed)
      : binding_(this) {
    binding_.set_error_handler(
        [occ = std::move(on_connection_closed)](zx_status_t /*status*/) { occ(); });
    binding_.Bind(std::move(buttons_interface), dispatcher);
  }

 private:
  // FIDL Interface Functions.
  void GetState(fuchsia::buttons::ButtonType type, GetStateCallback callback) override {}
  void RegisterNotify(uint8_t /*types*/, RegisterNotifyCallback callback) override {
    fuchsia::buttons::Buttons_RegisterNotify_Result status;
    status.set_err(ZX_OK);
    callback(std::move(status));
  }

  fidl::Binding<fuchsia::buttons::Buttons> binding_;
};

class FakeButtons : public ddk::ButtonsProtocol<FakeButtons> {
 public:
  FakeButtons()
      : ButtonsProtocol(),
        buttons_protocol_{&buttons_protocol_ops_, this},
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    loop_.StartThread("button_loop", &loop_thread_);
  }

  ~FakeButtons() { loop_.Shutdown(); }

  ddk::ButtonsProtocolClient client() { return ddk::ButtonsProtocolClient(&buttons_protocol_); }

  fake_ddk::ProtocolEntry ProtocolEntry() const {
    return {ZX_PROTOCOL_BUTTONS, *reinterpret_cast<const fake_ddk::Protocol*>(&buttons_protocol_)};
  }

  // |ZX_PROTOCOL_BUTTONS|
  zx_status_t ButtonsGetChannel(zx::channel chan) {
    fidl::InterfaceRequest<fuchsia::buttons::Buttons> buttons_interface(std::move(chan));
    buttons_ = std::make_unique<ButtonsImpl>(std::move(buttons_interface), loop_.dispatcher(),
                                             [this] { buttons_ = nullptr; });
    return ZX_OK;
  };

 private:
  buttons_protocol_t buttons_protocol_;
  std::unique_ptr<ButtonsImpl> buttons_;
  async::Loop loop_;
  thrd_t loop_thread_;
};

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_TEST_FAKE_BUTTONS_H_
