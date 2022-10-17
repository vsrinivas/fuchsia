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

  // OutgoingDirectory can be moved. Once moved, invoking a method on an
  // instance will yield undefined behavior.
  OutgoingDirectory(OutgoingDirectory&&) noexcept;
  OutgoingDirectory& operator=(OutgoingDirectory&&) noexcept;

  // OutgoingDirectory cannot be copied.
  OutgoingDirectory(const OutgoingDirectory&) = delete;
  OutgoingDirectory& operator=(const OutgoingDirectory&) = delete;

  // Adds an instance of a FIDL Service.
  //
  // A |handler| is added to provide an |instance| of a service.
  //
  // The template type |Service| must be the generated type representing a FIDL Service.
  // The generated class |Service::Handler| helps the caller populate a
  // |ServiceInstanceHandler|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The instance already exists.
  //
  // ZX_ERR_INVALID_ARGS: |instance| is an empty string or |handler| is empty.
  template <typename Service>
  zx::result<> AddService(ServiceInstanceHandler handler,
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
      return zx::make_result(ZX_ERR_INVALID_ARGS);
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

      zx::result<> status =
          component_outgoing_dir_.AddProtocolAt(std::move(outgoing_handler), basepath, member_name);
      if (status.is_error()) {
        return status;
      }
    }
    return zx::ok();
  }

  //
  // Wrappers around |component::OutgoingDirectory|.
  //
  // Protocols are not supported. Drivers should use |AddService| instead.
  //

  // Starts serving the outgoing directory on the given channel. This should
  // be invoked after the outgoing directory has been populated, e.g. via
  // |AddService|.
  //
  // This object will implement the |fuchsia.io.Directory| interface using this
  // channel. Note that this method returns immediately and that the |dispatcher|
  // provided to the constructor will be responsible for processing messages
  // sent to the server endpoint.
  //
  // # Errors
  //
  // ZX_ERR_BAD_HANDLE: |directory_server_end| is not a valid handle.
  //
  // ZX_ERR_ACCESS_DENIED: |directory_server_end| has insufficient rights.
  zx::result<> Serve(fidl::ServerEnd<fuchsia_io::Directory> directory_server_end) {
    return component_outgoing_dir_.Serve(std::move(directory_server_end));
  }

  // Serve a subdirectory at the root of this outgoing directory.
  //
  // The directory will be installed under the path |directory_name|. When
  // a request is received under this path, then it will be forwarded to
  // |remote_dir|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: An entry with the provided name already exists.
  //
  // ZX_ERR_BAD_HANDLE: |remote_dir| is an invalid handle.
  //
  // ZX_ERR_INVALID_ARGS: |directory_name| is an empty string.
  zx::result<> AddDirectory(fidl::ClientEnd<fuchsia_io::Directory> remote_dir,
                            cpp17::string_view directory_name) {
    return component_outgoing_dir_.AddDirectory(std::move(remote_dir), directory_name);
  }

  // Same as |AddDirectory| but allows setting the parent directory
  // in which the directory will be installed.
  zx::result<> AddDirectoryAt(fidl::ClientEnd<fuchsia_io::Directory> remote_dir,
                              cpp17::string_view path, cpp17::string_view directory_name) {
    return component_outgoing_dir_.AddDirectoryAt(std::move(remote_dir), path, directory_name);
  }

  // Removes an instance of a FIDL Service.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: The instance was not found.
  //
  // # Example
  //
  // ```
  // outgoing.RemoveService<lib_example::MyService>("my-instance");
  // ```
  template <typename Service>
  zx::result<> RemoveService(cpp17::string_view instance = kDefaultInstance) {
    return RemoveService(Service::Name, instance);
  }

  // Same as above but untyped.
  zx::result<> RemoveService(cpp17::string_view service,
                             cpp17::string_view instance = kDefaultInstance) {
    return component_outgoing_dir_.RemoveService(service, instance);
  }

  // Removes the subdirectory on the provided |directory_name|.
  //
  // # Errors
  //
  // ZX_ERR_NOT_FOUND: No entry was found with provided name.
  zx::result<> RemoveDirectory(cpp17::string_view directory_name) {
    return component_outgoing_dir_.RemoveDirectory(directory_name);
  }

  // Same as |RemoveDirectory| but allows specifying the parent directory
  // that the directory will be removed from. The parent directory, |path|,
  // will not be removed.
  zx::result<> RemoveDirectoryAt(cpp17::string_view path, cpp17::string_view directory_name) {
    return component_outgoing_dir_.RemoveDirectoryAt(path, directory_name);
  }

  // Get the underlying component outgoing directory. These APIs will only support
  // FIDL, they do not support DriverTransport.
  component::OutgoingDirectory& component() { return component_outgoing_dir_; }

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
