// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/test.structuredconfig.receiver.shim/cpp/wire.h>
#include <fidl/test.structuredconfig.receiver/cpp/markers.h>
#include <fidl/test.structuredconfig.receiver/cpp/wire.h>
#include <fidl/test.structuredconfig.receiver/cpp/wire_types.h>
#include <lib/async/cpp/executor.h>
#include <lib/driver2/logger.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/record_cpp.h>
#include <lib/fidl/cpp/wire/arena.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fidl/cpp/wire/vector_view.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>
#include <lib/inspect/component/cpp/component.h>
#include <lib/sys/component/cpp/handlers.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/assert.h>

#include <memory>
#include <optional>
#include <vector>

#include "fidl/test.structuredconfig.receiver.shim/cpp/wire_messaging.h"
#include "src/sys/component_manager/tests/structured_config/client_integration/cpp_driver/receiver_config.h"

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace scr = test_structuredconfig_receiver;
namespace scrs = test_structuredconfig_receiver_shim;

using fpromise::promise;

namespace {

template <typename T>
class DecodedObject {
 public:
  DecodedObject() = default;

  void Set(std::unique_ptr<uint8_t[]> raw_data, uint32_t size) {
    raw_data_ = std::move(raw_data);
    decoded_ = std::make_unique<fidl::unstable::DecodedMessage<T>>(
        fidl::internal::WireFormatVersion::kV2, raw_data_.get(), size);
    object = *decoded_->PrimaryObject();
  }

  T& Object() { return object; }
  bool ok() { return decoded_->ok(); }

 private:
  std::unique_ptr<uint8_t[]> raw_data_;
  T object;
  std::unique_ptr<fidl::unstable::DecodedMessage<T>> decoded_;
};

class ReceiverDriver : public fidl::WireServer<scr::ConfigReceiverPuppet> {
 public:
  ReceiverDriver(async_dispatcher_t* dispatcher, fidl::WireSharedClient<fdf::Node> node,
                 driver::Namespace ns, driver::Logger logger, receiver_config::Config config)
      : dispatcher_(dispatcher),
        outgoing_(component::OutgoingDirectory::Create(dispatcher)),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)),
        config_(std::move(config)) {}

  static constexpr const char* Name() { return "receiver"; }

  static zx::status<std::unique_ptr<ReceiverDriver>> Start(fdf::wire::DriverStartArgs& start_args,
                                                           fdf::UnownedDispatcher dispatcher,
                                                           fidl::WireSharedClient<fdf::Node> node,
                                                           driver::Namespace ns,
                                                           driver::Logger logger) {
    auto config = receiver_config::Config::TakeFromStartArgs(start_args);
    auto driver =
        std::make_unique<ReceiverDriver>(dispatcher->async_dispatcher(), std::move(node),
                                         std::move(ns), std::move(logger), std::move(config));
    auto result = driver->Run(std::move(start_args.outgoing_dir()));
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run(fidl::ServerEnd<::fuchsia_io::Directory> outgoing_dir) {
    component::ServiceHandler handler;
    scrs::ConfigService::Handler device(&handler);

    auto puppet = [this](fidl::ServerEnd<scr::ConfigReceiverPuppet> server_end) -> void {
      fidl::BindServer<fidl::WireServer<scr::ConfigReceiverPuppet>>(dispatcher_,
                                                                    std::move(server_end), this);
    };

    auto result = device.add_puppet(puppet);
    ZX_ASSERT(result.is_ok());

    result = outgoing_.AddService<scrs::ConfigService>(std::move(handler));
    ZX_ASSERT(result.is_ok());

    // Serve the inspect data
    auto config_node = inspector_.GetRoot().CreateChild("config");
    config_.RecordInspect(&config_node);
    inspector_.emplace(std::move(config_node));
    exposed_inspector_.emplace(inspect::ComponentInspector(outgoing_, dispatcher_, inspector_));

    return outgoing_.Serve(std::move(outgoing_dir));
  }

  void GetConfig(GetConfigRequestView request, GetConfigCompleter::Sync& _completer) override {
    scr::wire::ReceiverConfig receiver_config;

    fidl::Arena<65536> arena;

    auto bool_vector_view = fidl::VectorView<bool>(arena, config_.my_vector_of_flag().size());
    auto string_vector_view =
        fidl::VectorView<fidl::StringView>(arena, config_.my_vector_of_string().size());
    for (size_t i = 0; i < config_.my_vector_of_flag().size(); i++) {
      bool_vector_view[i] = config_.my_vector_of_flag()[i];
    }
    for (size_t i = 0; i < config_.my_vector_of_string().size(); i++) {
      string_vector_view[i] = fidl::StringView::FromExternal(config_.my_vector_of_string()[i]);
    }

    receiver_config.my_flag = config_.my_flag();
    receiver_config.my_int8 = config_.my_int8();
    receiver_config.my_int16 = config_.my_int16();
    receiver_config.my_int32 = config_.my_int32();
    receiver_config.my_int64 = config_.my_int64();
    receiver_config.my_uint8 = config_.my_uint8();
    receiver_config.my_uint16 = config_.my_uint16();
    receiver_config.my_uint32 = config_.my_uint32();
    receiver_config.my_uint64 = config_.my_uint64();
    receiver_config.my_string = fidl::StringView::FromExternal(config_.my_string());
    receiver_config.my_vector_of_flag = bool_vector_view;
    receiver_config.my_vector_of_uint8 =
        fidl::VectorView<uint8_t>::FromExternal(config_.my_vector_of_uint8());
    receiver_config.my_vector_of_uint16 =
        fidl::VectorView<uint16_t>::FromExternal(config_.my_vector_of_uint16());
    receiver_config.my_vector_of_uint32 =
        fidl::VectorView<uint32_t>::FromExternal(config_.my_vector_of_uint32());
    receiver_config.my_vector_of_uint64 =
        fidl::VectorView<uint64_t>::FromExternal(config_.my_vector_of_uint64());
    receiver_config.my_vector_of_int8 =
        fidl::VectorView<int8_t>::FromExternal(config_.my_vector_of_int8());
    receiver_config.my_vector_of_int16 =
        fidl::VectorView<int16_t>::FromExternal(config_.my_vector_of_int16());
    receiver_config.my_vector_of_int32 =
        fidl::VectorView<int32_t>::FromExternal(config_.my_vector_of_int32());
    receiver_config.my_vector_of_int64 =
        fidl::VectorView<int64_t>::FromExternal(config_.my_vector_of_int64());
    receiver_config.my_vector_of_string = string_vector_view;

    _completer.Reply(receiver_config);
  }

  async_dispatcher_t* const dispatcher_;
  component::OutgoingDirectory outgoing_;
  fidl::WireSharedClient<fdf::Node> node_;
  fidl::WireSharedClient<fdf::NodeController> controller_;
  driver::Namespace ns_;
  driver::Logger logger_;
  receiver_config::Config config_;
  inspect::Inspector inspector_;
  std::optional<inspect::ComponentInspector> exposed_inspector_ = std::nullopt;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(ReceiverDriver);
