// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.structuredconfig.receiver.shim/cpp/wire.h>
#include <lib/driver2/driver2_cpp.h>
#include <lib/inspect/component/cpp/component.h>

#include "src/sys/component_manager/tests/structured_config/client_integration/cpp_driver/receiver_config.h"

namespace scr = test_structuredconfig_receiver;
namespace scrs = test_structuredconfig_receiver_shim;

namespace {

class ReceiverDriver : public driver::DriverBase,
                       public fidl::WireServer<scr::ConfigReceiverPuppet> {
 public:
  ReceiverDriver(driver::DriverStartArgs start_args, fdf::UnownedDispatcher driver_dispatcher)
      : driver::DriverBase("receiver", std::move(start_args), std::move(driver_dispatcher)),
        config_(take_config<receiver_config::Config>()) {}

  zx::status<> Start() override {
    driver::ServiceInstanceHandler handler;
    scrs::ConfigService::Handler device(&handler);

    auto puppet = [this](fidl::ServerEnd<scr::ConfigReceiverPuppet> server_end) -> void {
      fidl::BindServer<fidl::WireServer<scr::ConfigReceiverPuppet>>(dispatcher(),
                                                                    std::move(server_end), this);
    };

    auto result = device.add_puppet(puppet);
    ZX_ASSERT(result.is_ok());

    result = context().outgoing()->AddService<scrs::ConfigService>(std::move(handler));
    ZX_ASSERT(result.is_ok());

    // Serve the inspect data
    auto config_node = inspector_.GetRoot().CreateChild("config");
    config_.RecordInspect(&config_node);
    inspector_.emplace(std::move(config_node));
    exposed_inspector_.emplace(
        inspect::ComponentInspector(context().outgoing()->component(), dispatcher(), inspector_));

    return zx::ok();
  }

 private:
  void GetConfig(GetConfigCompleter::Sync& _completer) override {
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

  receiver_config::Config config_;
  inspect::Inspector inspector_;
  std::optional<inspect::ComponentInspector> exposed_inspector_ = std::nullopt;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V2(driver::Record<ReceiverDriver>);
