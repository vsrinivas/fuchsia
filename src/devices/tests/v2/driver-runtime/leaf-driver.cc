// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.runtime.test/cpp/driver/wire.h>
#include <fidl/fuchsia.runtime.test/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/promise.h>
#include <lib/driver2/record_cpp.h>
#include <lib/driver2/runtime.h>
#include <lib/fdf/cpp/arena.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace ft = fuchsia_runtime_test;

using fpromise::error;
using fpromise::ok;
using fpromise::promise;
using fpromise::result;

namespace {

class LeafDriver {
 public:
  LeafDriver(fdf::UnownedDispatcher dispatcher, fidl::WireSharedClient<fdf::Node> node,
             driver::Namespace ns, driver::Logger logger)
      : dispatcher_(dispatcher->async_dispatcher()),
        executor_(dispatcher->async_dispatcher()),
        fdf_dispatcher_(std::move(dispatcher)),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)) {}

  static constexpr const char* Name() { return "leaf"; }

  static zx::status<std::unique_ptr<LeafDriver>> Start(fdf::wire::DriverStartArgs& start_args,
                                                       fdf::UnownedDispatcher dispatcher,
                                                       fidl::WireSharedClient<fdf::Node> node,
                                                       driver::Namespace ns,
                                                       driver::Logger logger) {
    auto driver = std::make_unique<LeafDriver>(std::move(dispatcher), std::move(node),
                                               std::move(ns), std::move(logger));
    driver->Run();
    return zx::ok(std::move(driver));
  }

  async_dispatcher_t* const dispatcher_;

 private:
  void Run() {
    auto task = driver::Connect<fdf::RuntimeConnector>(ns_, dispatcher_)
                    .and_then([&](fidl::WireSharedClient<fdf::RuntimeConnector>& client) {
                      runtime_connector_ = std::move(client);
                      return driver::ConnectToRuntimeProtocol<ft::DriverTransportProtocol>(
                          runtime_connector_);
                    })
                    .and_then([&](fdf::Channel& channel) { runtime_ = std::move(channel); })
                    .and_then(fit::bind_member(this, &LeafDriver::CallParent))
                    .and_then(fit::bind_member(this, &LeafDriver::ReadReplyFromParent))
                    .and_then(driver::Connect<ft::Waiter>(ns_, dispatcher_))
                    .and_then(fit::bind_member(this, &LeafDriver::CallAck))
                    .or_else(fit::bind_member(this, &LeafDriver::UnbindNode))
                    .wrap_with(scope_);
    executor_.schedule_task(std::move(task));
  }

  fpromise::result<void, zx_status_t> CallParentWithResult() {
    auto arena = fdf::Arena::Create(0, "");
    if (arena.is_error()) {
      return fpromise::error(arena.status_value());
    }
    auto res = runtime_.Write(0, *std::move(arena), 0, 0, cpp20::span<zx_handle_t>());
    if (res.is_error()) {
      return fpromise::error(res.status_value());
    }
    return fpromise::ok();
  }

  fpromise::promise<void, zx_status_t> CallParent() {
    return fpromise::make_result_promise(CallParentWithResult());
  }

  fpromise::promise<void, zx_status_t> ReadReplyFromParent() {
    fpromise::bridge<void, zx_status_t> bridge;
    auto callback = [completer = std::move(bridge.completer), this](fdf_dispatcher_t* dispatcher,
                                                                    fdf::ChannelRead* channel_read,
                                                                    fdf_status_t status) mutable {
      if (status != ZX_OK) {
        FDF_LOG(ERROR, "ChannelRead callback got failed status: %u", status);
        completer.complete_error(status);
        return;
      }
      fdf::UnownedChannel channel(channel_read->channel());
      auto read_return = channel->Read(0);
      if (read_return.is_error()) {
        FDF_LOG(ERROR, "Channel::Read failed, got error: %s",
                zx_status_get_string(read_return.status_value()));
        completer.complete_error(read_return.status_value());
        return;
      }
      if (read_return->num_bytes != sizeof(ft::wire::kParentDeviceTestData)) {
        FDF_LOG(ERROR, "Channel::Read got %u bytes, expected %lu", read_return->num_bytes,
                sizeof(ft::wire::kParentDeviceTestData));
        completer.complete_error(read_return.status_value());
        return;
      }
      auto data = static_cast<uint32_t*>(read_return->data);
      if (*data != ft::wire::kParentDeviceTestData) {
        FDF_LOG(ERROR, "Got unexpected data from parent: %u, expected: %u", *data,
                ft::wire::kParentDeviceTestData);
        completer.complete_error(ZX_ERR_BAD_STATE);
        return;
      }
      completer.complete_ok();
      delete channel_read;
    };

    auto channel_read =
        std::make_unique<fdf::ChannelRead>(runtime_.get(), 0 /* options */, std::move(callback));
    channel_read->Begin(fdf_dispatcher_->get());
    channel_read.release();

    return bridge.consumer.promise_or(fpromise::error(ZX_ERR_INTERNAL));
  }

  result<void, zx_status_t> CallAck(const fidl::WireSharedClient<ft::Waiter>& waiter) {
    __UNUSED auto result = waiter->Ack();
    return ok();
  }

  result<> UnbindNode(const zx_status_t& status) {
    FDF_LOG(ERROR, "Failed to start leaf driver: %s", zx_status_get_string(status));
    node_.AsyncTeardown();
    return ok();
  }

  async::Executor executor_;
  fdf::UnownedDispatcher const fdf_dispatcher_;

  fidl::WireSharedClient<fdf::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;

  fidl::WireSharedClient<fdf::RuntimeConnector> runtime_connector_;
  fdf::Channel runtime_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(LeafDriver);
