// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.runtime.test/cpp/wire.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/promise.h>
#include <lib/driver2/record_cpp.h>
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
      : dispatcher_(dispatcher),
        executor_(dispatcher->async_dispatcher()),
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

 private:
  void Run() {
    // Test we can block on the dispatcher thread.
    ZX_ASSERT(ZX_OK == DoHandshakeSynchronously());

    auto task = driver::Connect<ft::Waiter>(ns_, dispatcher_->async_dispatcher())
                    .and_then(fit::bind_member(this, &LeafDriver::CallAck))
                    .or_else(fit::bind_member(this, &LeafDriver::UnbindNode))
                    .wrap_with(scope_);
    executor_.schedule_task(std::move(task));
  }

  zx_status_t DoHandshakeSynchronously() {
    ZX_ASSERT((*dispatcher_->options() & FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS) ==
              FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS);

    std::string_view path = fidl::DiscoverableProtocolDefaultPath<ft::Handshake>;
    fuchsia_io::wire::OpenFlags flags = fuchsia_io::wire::OpenFlags::kRightReadable;

    auto result = ns_.Connect<ft::Handshake>(path, flags);
    if (result.is_error()) {
      return result.status_value();
    }
    fidl::WireSyncClient<ft::Handshake> client(std::move(*result));
    return client->Do().status();
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

  fdf::UnownedDispatcher const dispatcher_;
  async::Executor executor_;

  fidl::WireSharedClient<fdf::Node> node_;
  driver::Namespace ns_;
  driver::Logger logger_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(LeafDriver);
