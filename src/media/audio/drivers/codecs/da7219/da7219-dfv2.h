// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_DFV2_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_DFV2_H_

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <lib/driver/compat/cpp/context.h>
#include <lib/driver/component/cpp/driver_cpp.h>

#include <algorithm>
#include <memory>

#include "src/media/audio/drivers/codecs/da7219/da7219-server.h"

namespace audio::da7219 {

// CodecConnector is a service-hub/trampoline mechanism to allow DFv1 Codec drivers to service
// FIDL outside DFv1, not needed in DFv2 but still in used by all DFv1 drivers and clients.
// ServerConnector allows serving CodecConnector FIDL providing the trampoline and also
// allows binding the server directly via BindServer.
class ServerConnector : public fidl::WireServer<fuchsia_hardware_audio::CodecConnector> {
 public:
  explicit ServerConnector(Logger* logger, std::shared_ptr<Core> core, bool is_input)
      : logger_(logger), core_(core), is_input_(is_input) {}

 private:
  // Bind the server without the trampoline.
  void BindServer(fidl::ServerEnd<fuchsia_hardware_audio::Codec> request) {
    auto on_unbound = [this](fidl::WireServer<fuchsia_hardware_audio::Codec>*,
                             fidl::UnbindInfo info,
                             fidl::ServerEnd<fuchsia_hardware_audio::Codec> server_end) {
      if (info.is_peer_closed()) {
        DA7219_LOG(DEBUG, "Client disconnected");
      } else if (!info.is_user_initiated() && info.status() != ZX_ERR_CANCELED) {
        // Do not log canceled cases which happens too often in particular in test cases.
        DA7219_LOG(ERROR, "Client connection unbound: %s", info.status_string());
      }
      server_.reset();  // Allow re-connecting after unbind.
    };
    server_ = std::make_unique<Server>(logger_, core_, is_input_);
    fidl::BindServer(core_->dispatcher(), std::move(request), server_.get(), std::move(on_unbound));
  }

  // LLCPP implementation for the CodecConnector API.
  void Connect(fuchsia_hardware_audio::wire::CodecConnectorConnectRequest* request,
               ConnectCompleter::Sync& completer) override {
    if (server_) {
      completer.Close(ZX_ERR_NO_RESOURCES);  // Only allow one connection.
      return;
    }
    BindServer(std::move(request->codec_protocol));
  }

  Logger* logger_;
  std::shared_ptr<Core> core_;
  bool is_input_;
  std::unique_ptr<Server> server_;
};

class Driver : public driver::DriverBase {
 public:
  Driver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : driver::DriverBase("da7219", std::move(start_args), std::move(driver_dispatcher)) {}

  ~Driver() override = default;

  zx::result<> Start() override;

 private:
  zx::result<> Serve(std::string_view name, bool is_input);
  zx::result<fidl::ClientEnd<fuchsia_hardware_i2c::Device>> GetI2cClient() const;
  zx::result<zx::interrupt> GetIrq() const;

  std::shared_ptr<Core> core_;
  std::shared_ptr<ServerConnector> server_output_;
  std::shared_ptr<ServerConnector> server_input_;
  std::unique_ptr<compat::Context> compat_context_;
};

}  // namespace audio::da7219

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_DFV2_H_
