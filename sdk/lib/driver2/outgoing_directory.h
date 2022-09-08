// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_OUTGOING_DIRECTORY_H_
#define LIB_DRIVER2_OUTGOING_DIRECTORY_H_

#include <lib/driver2/handlers.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/cpp/protocol.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fidl_driver/cpp/transport.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

namespace driver {

// The name of the default FIDL Service instance.
constexpr const std::string_view kDefaultInstance = component::kDefaultInstance;

// Driver specific implementation for an outgoing directory that wraps around
// |component::OutgoingDirectory|.
class OutgoingDirectory final {
 public:
  static OutgoingDirectory Create(fdf_dispatcher_t* dispatcher) {
    ZX_ASSERT_MSG(dispatcher != nullptr,
                  "OutgoingDirectory::Create received nullptr |dispatcher|.");
    auto component_outgoing_dir =
        component::OutgoingDirectory::Create(fdf_dispatcher_get_async_dispatcher(dispatcher));
    return OutgoingDirectory(std::move(component_outgoing_dir), dispatcher);
  }

  OutgoingDirectory(component::OutgoingDirectory component_outgoing_dir,
                    fdf_dispatcher_t* dispatcher)
      : component_outgoing_dir_(std::move(component_outgoing_dir)), dispatcher_(dispatcher) {}

  template <typename Service>
  zx::status<> AddService(ServiceInstanceHandler handler,
                          cpp17::string_view instance = kDefaultInstance) {
    bool is_channel_transport = false;
    if constexpr (std::is_same_v<typename Service::Transport, fidl::internal::ChannelTransport>) {
      is_channel_transport = true;
    } else if constexpr (std::is_same_v<typename Service::Transport,
                                        fidl::internal::DriverTransport>) {
    } else {
      static_assert(always_false<Service>);
    }

    auto handlers = handler.TakeMemberHandlers();
    if (handlers.empty()) {
      return zx::make_status(ZX_ERR_INVALID_ARGS);
    }

    std::string basepath =
        std::string("svc") + "/" + std::string(Service::Name) + "/" + std::string(instance);

    for (auto& [member_name, member_handler] : handlers) {
      auto outgoing_handler = [this, is_channel_transport, handler = std::move(member_handler)](
                                  zx::channel server_end) mutable {
        ZX_ASSERT(server_end.is_valid());
        if (is_channel_transport) {
          handler(fidl::internal::MakeAnyTransport(std::move(server_end)));
          return;
        }
        // The received |server_end| is the token channel handle, which needs to be registered
        // with the runtime.
        RegisterRuntimeToken(std::move(server_end), handler.share());
      };

      zx::status<> status = component_outgoing_dir().AddProtocolAt(std::move(outgoing_handler),
                                                                   basepath, member_name);
      if (status.is_error()) {
        return status;
      }
    }
    return zx::ok();
  }

  // Wrappers around |component::OutgoingDirectory|.

  zx::status<> Serve(fidl::ServerEnd<fuchsia_io::Directory> directory_server_end) {
    return component_outgoing_dir().Serve(std::move(directory_server_end));
  }

  // TODO(fxbug.dev/108374): implement the rest of the |OutgoingDirectory| wraoper functions
  // and make |component_outgoing_dir| private.

  component::OutgoingDirectory& component_outgoing_dir() { return component_outgoing_dir_; }

 private:
  template <typename T>
  static constexpr std::false_type always_false{};

  // Registers |token| with the driver runtime. When the client attempts to connect using the
  // token channel peer, we will call |handler|.
  void RegisterRuntimeToken(zx::channel token, AnyHandler handler);

  component::OutgoingDirectory component_outgoing_dir_;

  fdf_dispatcher_t* dispatcher_ = nullptr;
};

}  // namespace driver

#endif  // LIB_DRIVER2_OUTGOING_DIRECTORY_H_
