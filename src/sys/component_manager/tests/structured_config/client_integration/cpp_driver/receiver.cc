// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/test.structuredconfig.receiver.shim/cpp/wire.h>
#include <fidl/test.structuredconfig.receiver/cpp/markers.h>
#include <fidl/test.structuredconfig.receiver/cpp/wire.h>
#include <fidl/test.structuredconfig.receiver/cpp/wire_types.h>
#include <lib/async/cpp/executor.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/scope.h>
#include <lib/service/llcpp/outgoing_directory.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
#include <zircon/assert.h>

#include <memory>
#include <vector>

#include <bind/fuchsia/test/cpp/fidl.h>

#include "fidl/test.structuredconfig.receiver.shim/cpp/wire_messaging.h"
#include "src/devices/lib/driver2/inspect.h"
#include "src/devices/lib/driver2/logger.h"
#include "src/devices/lib/driver2/namespace.h"
#include "src/devices/lib/driver2/record_cpp.h"

namespace fdf = fuchsia_driver_framework;
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
                 driver::Namespace ns, driver::Logger logger, zx::vmo config_vmo)
      : dispatcher_(dispatcher),
        outgoing_(dispatcher),
        node_(std::move(node)),
        ns_(std::move(ns)),
        logger_(std::move(logger)) {
    get_config(std::move(config_vmo));
  }

  static constexpr const char* Name() { return "receiver"; }

  static zx::status<std::unique_ptr<ReceiverDriver>> Start(fdf::wire::DriverStartArgs& start_args,
                                                           async_dispatcher_t* dispatcher,
                                                           fidl::WireSharedClient<fdf::Node> node,
                                                           driver::Namespace ns,
                                                           driver::Logger logger) {
    ZX_ASSERT_MSG(start_args.has_config(), "No config object found in driver start args");
    auto driver =
        std::make_unique<ReceiverDriver>(dispatcher, std::move(node), std::move(ns),
                                         std::move(logger), std::move(start_args.config()));
    auto result = driver->Run(std::move(start_args.outgoing_dir()));
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(driver));
  }

 private:
  zx::status<> Run(fidl::ServerEnd<::fuchsia_io::Directory> outgoing_dir) {
    service::ServiceHandler handler;
    scrs::ConfigService::Handler device(&handler);

    auto puppet = [this](fidl::ServerEnd<scr::ConfigReceiverPuppet> server_end) -> zx::status<> {
      fidl::BindServer<fidl::WireServer<scr::ConfigReceiverPuppet>>(dispatcher_,
                                                                    std::move(server_end), this);
      return zx::ok();
    };

    auto result = device.add_puppet(puppet);
    ZX_ASSERT(result.is_ok());

    result = outgoing_.AddService<scrs::ConfigService>(std::move(handler));
    ZX_ASSERT(result.is_ok());

    // Serve the inspect data
    record_to_inspect(&inspector_);
    auto vmo_result = driver::ExposeInspector(inspector_, outgoing_.root_dir());
    ZX_ASSERT(vmo_result.is_ok());
    inspect_vmo_ = std::move(vmo_result.value());

    return outgoing_.Serve(std::move(outgoing_dir));
  }

  // TODO(https://fxbug.dev/91978): Replace this method with a call to the client library.
  void get_config(zx::vmo config_vmo) {
    // Get the size of the VMO
    uint64_t content_size_prop = 0;
    zx_status_t status = config_vmo.get_prop_content_size(&content_size_prop);
    ZX_ASSERT_MSG(status == ZX_OK, "Could not get content size of config VMO");
    size_t vmo_content_size = static_cast<size_t>(content_size_prop);

    // Checksum length must be correct
    uint16_t checksum_length = 0;
    status = config_vmo.read(&checksum_length, 0, 2);
    ZX_ASSERT_MSG(status == ZX_OK, "Could not read checksum length from config VMO");

    // Verify Checksum
    std::vector<uint8_t> checksum(checksum_length);
    status = config_vmo.read(checksum.data(), 2, checksum_length);
    ZX_ASSERT_MSG(status == ZX_OK, "Could not read checksum from config VMO");
    std::vector<uint8_t> expected_checksum{0xcd, 0x57, 0xb2, 0xa2, 0x89, 0xbb, 0xb6, 0x11,
                                           0xcf, 0x81, 0x50, 0xec, 0x06, 0xc5, 0x06, 0x4c,
                                           0x7c, 0xae, 0x79, 0x0f, 0xaa, 0x73, 0x0b, 0x6f,
                                           0xa1, 0x02, 0xc3, 0x53, 0x7b, 0x94, 0xee, 0x1a};
    ZX_ASSERT_MSG(checksum == expected_checksum, "Invalid checksum for config VMO");

    // Read the FIDL struct into memory
    // Skip the checksum length + checksum + FIDL persistent header
    // Align the struct pointer to 8 bytes (as required by FIDL)
    size_t header = 2 + checksum_length + 8;
    size_t fidl_struct_size = vmo_content_size - header;
    std::unique_ptr<uint8_t[]> data(new uint8_t[fidl_struct_size]);
    status = config_vmo.read(data.get(), header, fidl_struct_size);
    ZX_ASSERT_MSG(status == ZX_OK, "Could not read FIDL struct from config VMO");

    config_.Set(std::move(data), static_cast<uint32_t>(fidl_struct_size));
    ZX_ASSERT_MSG(config_.ok(), "Could not decode FIDL config from VMO");
  }

  // TODO(https://fxbug.dev/91978): Replace this method with a call to the client library.
  void record_to_inspect(inspect::Inspector* inspector) {
    inspect::Node inspect_config = inspector->GetRoot().CreateChild("config");
    inspect_config.CreateBool("my_flag", config_.Object().my_flag, inspector);

    inspect_config.CreateInt("my_int16", config_.Object().my_int16, inspector);

    inspect_config.CreateInt("my_int32", config_.Object().my_int32, inspector);

    inspect_config.CreateInt("my_int64", config_.Object().my_int64, inspector);

    inspect_config.CreateInt("my_int8", config_.Object().my_int8, inspector);

    inspect_config.CreateString("my_string", config_.Object().my_string.data(), inspector);

    inspect_config.CreateUint("my_uint16", config_.Object().my_uint16, inspector);

    inspect_config.CreateUint("my_uint32", config_.Object().my_uint32, inspector);

    inspect_config.CreateUint("my_uint64", config_.Object().my_uint64, inspector);

    inspect_config.CreateUint("my_uint8", config_.Object().my_uint8, inspector);

    auto my_vector_of_flag = inspect_config.CreateUintArray(
        "my_vector_of_flag", config_.Object().my_vector_of_flag.count());
    for (size_t i = 0; i < config_.Object().my_vector_of_flag.count(); i++) {
      my_vector_of_flag.Set(i, config_.Object().my_vector_of_flag[i]);
    }
    inspector->emplace(std::move(my_vector_of_flag));

    auto my_vector_of_int16 = inspect_config.CreateIntArray(
        "my_vector_of_int16", config_.Object().my_vector_of_int16.count());
    for (size_t i = 0; i < config_.Object().my_vector_of_int16.count(); i++) {
      my_vector_of_int16.Set(i, config_.Object().my_vector_of_int16[i]);
    }
    inspector->emplace(std::move(my_vector_of_int16));

    auto my_vector_of_int32 = inspect_config.CreateIntArray(
        "my_vector_of_int32", config_.Object().my_vector_of_int32.count());
    for (size_t i = 0; i < config_.Object().my_vector_of_int32.count(); i++) {
      my_vector_of_int32.Set(i, config_.Object().my_vector_of_int32[i]);
    }
    inspector->emplace(std::move(my_vector_of_int32));

    auto my_vector_of_int64 = inspect_config.CreateIntArray(
        "my_vector_of_int64", config_.Object().my_vector_of_int64.count());
    for (size_t i = 0; i < config_.Object().my_vector_of_int64.count(); i++) {
      my_vector_of_int64.Set(i, config_.Object().my_vector_of_int64[i]);
    }
    inspector->emplace(std::move(my_vector_of_int64));

    auto my_vector_of_int8 = inspect_config.CreateIntArray(
        "my_vector_of_int8", config_.Object().my_vector_of_int8.count());
    for (size_t i = 0; i < config_.Object().my_vector_of_int8.count(); i++) {
      my_vector_of_int8.Set(i, config_.Object().my_vector_of_int8[i]);
    }
    inspector->emplace(std::move(my_vector_of_int8));

    auto my_vector_of_string = inspect_config.CreateStringArray(
        "my_vector_of_string", config_.Object().my_vector_of_string.count());
    for (size_t i = 0; i < config_.Object().my_vector_of_string.count(); i++) {
      auto ref = std::string_view(config_.Object().my_vector_of_string[i].data());
      my_vector_of_string.Set(i, ref);
    }
    inspector->emplace(std::move(my_vector_of_string));

    auto my_vector_of_uint16 = inspect_config.CreateUintArray(
        "my_vector_of_uint16", config_.Object().my_vector_of_uint16.count());
    for (size_t i = 0; i < config_.Object().my_vector_of_uint16.count(); i++) {
      my_vector_of_uint16.Set(i, config_.Object().my_vector_of_uint16[i]);
    }
    inspector->emplace(std::move(my_vector_of_uint16));

    auto my_vector_of_uint32 = inspect_config.CreateUintArray(
        "my_vector_of_uint32", config_.Object().my_vector_of_uint32.count());
    for (size_t i = 0; i < config_.Object().my_vector_of_uint32.count(); i++) {
      my_vector_of_uint32.Set(i, config_.Object().my_vector_of_uint32[i]);
    }
    inspector->emplace(std::move(my_vector_of_uint32));

    auto my_vector_of_uint64 = inspect_config.CreateUintArray(
        "my_vector_of_uint64", config_.Object().my_vector_of_uint64.count());
    for (size_t i = 0; i < config_.Object().my_vector_of_uint64.count(); i++) {
      my_vector_of_uint64.Set(i, config_.Object().my_vector_of_uint64[i]);
    }
    inspector->emplace(std::move(my_vector_of_uint64));

    auto my_vector_of_uint8 = inspect_config.CreateUintArray(
        "my_vector_of_uint8", config_.Object().my_vector_of_uint8.count());
    for (size_t i = 0; i < config_.Object().my_vector_of_uint8.count(); i++) {
      my_vector_of_uint8.Set(i, config_.Object().my_vector_of_uint8[i]);
    }
    inspector->emplace(std::move(my_vector_of_uint8));

    inspector->emplace(std::move(inspect_config));
  }

  void GetConfig(GetConfigRequestView request, GetConfigCompleter::Sync& _completer) override {
    _completer.Reply(config_.Object());
  }

  async_dispatcher_t* const dispatcher_;
  service::OutgoingDirectory outgoing_;
  fidl::WireSharedClient<fdf::Node> node_;
  fidl::WireSharedClient<fdf::NodeController> controller_;
  driver::Namespace ns_;
  driver::Logger logger_;
  DecodedObject<scr::wire::ReceiverConfig> config_;
  inspect::Inspector inspector_;
  zx::vmo inspect_vmo_;

  // NOTE: Must be the last member.
  fpromise::scope scope_;
};

}  // namespace

FUCHSIA_DRIVER_RECORD_CPP_V1(ReceiverDriver);
