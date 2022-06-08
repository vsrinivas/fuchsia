// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.component.decl/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.runtime.test/cpp/driver/wire.h>
#include <fidl/fuchsia.runtime.test/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/promise.h>
#include <lib/driver2/record_cpp.h>
#include <lib/driver2/runtime.h>
#include <lib/driver2/runtime_connector_impl.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>

#include <bind/fuchsia/test/cpp/bind.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace fcd = fuchsia_component_decl;
namespace fio = fuchsia_io;
namespace ft = fuchsia_runtime_test;

using fpromise::error;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

namespace {

class RootDriver : public driver::RuntimeConnectorImpl {
 public:
  RootDriver(fdf::UnownedDispatcher dispatcher, fidl::WireSharedClient<fdf::Node> node,
             driver::Namespace ns, driver::Logger logger)
      : driver::RuntimeConnectorImpl(dispatcher->async_dispatcher()),
        dispatcher_(dispatcher->async_dispatcher()),
        executor_(dispatcher->async_dispatcher()),
        outgoing_(component::OutgoingDirectory::Create(dispatcher->async_dispatcher())),
        fdf_dispatcher_(std::move(dispatcher)),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)) {}

  static constexpr const char* Name() { return "root"; }

  static zx::status<std::unique_ptr<RootDriver>> Start(fdf::wire::DriverStartArgs& start_args,
                                                       fdf::UnownedDispatcher dispatcher,
                                                       fidl::WireSharedClient<fdf::Node> node,
                                                       driver::Namespace ns,
                                                       driver::Logger logger) {
    auto driver = std::make_unique<RootDriver>(std::move(dispatcher), std::move(node),
                                               std::move(ns), std::move(logger));
    auto result = driver->Run(std::move(start_args.outgoing_dir()));
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

  zx_status_t RegisterProtocolHandler(fdf::Channel channel) {
    // Wait for messages from the child.
    auto channel_read = std::make_unique<fdf::ChannelRead>(
        channel.release(), 0 /* options */,
        [this](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read, fdf_status_t status) {
          if (status == ZX_OK) {
            zx_status_t status =
                HandleChildRuntimeRequest(fdf::UnownedChannel(channel_read->channel()));
            if (status == ZX_OK) {
              status = channel_read->Begin(fdf_dispatcher_->get());
              if (status == ZX_OK) {
                return;
              }
            }
          }
          fdf_handle_close(channel_read->channel());
          delete channel_read;
        });
    zx_status_t status = channel_read->Begin(fdf_dispatcher_->get());
    if (status != ZX_OK) {
      return status;
    }
    channel_read.release();
    return ZX_OK;
  }

  zx_status_t HandleChildRuntimeRequest(fdf::UnownedChannel channel) {
    auto read_return = channel->Read(0);
    if (read_return.is_error()) {
      FDF_LOG(ERROR, "HandleChildRuntimeRequest got unexpected read error: %s",
              zx_status_get_string(read_return.status_value()));
      return read_return.status_value();
    }

    uint32_t data = ft::wire::kParentDeviceTestData;
    void* ptr = read_return->arena.Allocate(sizeof(data));
    memcpy(ptr, &data, sizeof(data));

    auto write_status =
        channel->Write(0, read_return->arena, ptr, sizeof(data), cpp20::span<zx_handle_t>());
    if (write_status.is_error()) {
      FDF_LOG(ERROR, "HandleChildRuntimeRequest got unexpected write error: %s",
              zx_status_get_string(write_status.status_value()));
      return write_status.status_value();
    }
    return ZX_OK;
  }

 private:
  zx::status<> Run(fidl::ServerEnd<fio::Directory> outgoing_dir) {
    // Setup the outgoing directory.
    auto service = [this](fidl::ServerEnd<fdf::RuntimeConnector> server_end) {
      fidl::BindServer(dispatcher_, std::move(server_end), this);
    };
    zx::status<> status = outgoing_.AddProtocol<fdf::RuntimeConnector>(std::move(service));
    if (status.is_error()) {
      return status;
    }

    auto serve = outgoing_.Serve(std::move(outgoing_dir));
    if (serve.is_error()) {
      return serve.take_error();
    }

    RegisterProtocol(fidl::DiscoverableProtocolName<ft::DriverTransportProtocol>,
                     fit::bind_member(this, &RootDriver::RegisterProtocolHandler));

    // Start the driver.
    auto task =
        AddChild().or_else(fit::bind_member(this, &RootDriver::UnbindNode)).wrap_with(scope_);
    executor_.schedule_task(std::move(task));
    return zx::ok();
  }

  promise<void, fdf::wire::NodeError> AddChild() {
    fidl::Arena arena;

    // Offer `fuchsia.test.Handshake` to the driver that binds to the node.
    auto protocol = fcd::wire::OfferProtocol::Builder(arena)
                        .source_name(fidl::StringView::FromExternal(
                            fidl::DiscoverableProtocolName<fdf::RuntimeConnector>))
                        .target_name(fidl::StringView::FromExternal(
                            fidl::DiscoverableProtocolName<fdf::RuntimeConnector>))
                        .dependency_type(fcd::wire::DependencyType::kStrong)
                        .Build();
    fcd::wire::Offer offer = fcd::wire::Offer::WithProtocol(arena, std::move(protocol));

    // Set the properties of the node that a driver will bind to.
    auto property = fdf::wire::NodeProperty::Builder(arena)
                        .key(fdf::wire::NodePropertyKey::WithIntValue(1 /* BIND_PROTOCOL */))
                        .value(fdf::wire::NodePropertyValue::WithIntValue(
                            bind_fuchsia_test::BIND_PROTOCOL_DEVICE))
                        .Build();

    auto args =
        fdf::wire::NodeAddArgs::Builder(arena)
            .name("leaf")
            .properties(fidl::VectorView<fdf::wire::NodeProperty>::FromExternal(&property, 1))
            .offers(fidl::VectorView<fcd::wire::Offer>::FromExternal(&offer, 1))
            .Build();

    // Create endpoints of the `NodeController` for the node.
    auto endpoints = fidl::CreateEndpoints<fdf::NodeController>();
    if (endpoints.is_error()) {
      return fpromise::make_error_promise(fdf::wire::NodeError::kInternal);
    }

    return driver::AddChild(node_, std::move(args), std::move(endpoints->server), {})
        .and_then([this, client = std::move(endpoints->client)]() mutable {
          controller_.Bind(std::move(client), dispatcher_);
        });
  }

  result<> UnbindNode(const fdf::wire::NodeError& error) {
    FDF_LOG(ERROR, "Failed to start root driver: %d", error);
    node_.AsyncTeardown();
    return ok();
  }

  async_dispatcher_t* const dispatcher_;
  async::Executor executor_;
  component::OutgoingDirectory outgoing_;
  fdf::UnownedDispatcher const fdf_dispatcher_;

  fidl::WireSharedClient<fdf::Node> node_;
  fidl::WireSharedClient<fdf::NodeController> controller_;
  driver::Namespace ns_;
  driver::Logger logger_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(RootDriver);
