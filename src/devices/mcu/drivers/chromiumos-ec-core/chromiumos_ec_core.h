// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_CHROMIUMOS_EC_CORE_H_
#define SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_CHROMIUMOS_EC_CORE_H_

#include <fidl/fuchsia.hardware.google.ec/cpp/wire_messaging.h>
#include <fidl/fuchsia.hardware.google.ec/cpp/wire_types.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/ddk/debug.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/svc/outgoing.h>

#include <chromiumos-platform-ec/ec_commands.h>
#include <ddktl/device.h>

#include "src/devices/lib/acpi/client.h"

namespace chromiumos_ec_core {

// Keys used for inspect nodes/properties.
inline constexpr const char* kNodeCore = "core";
inline constexpr const char* kPropVersionRo = "version-ro";
inline constexpr const char* kPropVersionRw = "version-rw";
inline constexpr const char* kPropCurrentImage = "current-image";
inline constexpr const char* kPropBuildInfo = "build-info";
inline constexpr const char* kPropChipVendor = "chip-vendor";
inline constexpr const char* kPropChipName = "chip-name";
inline constexpr const char* kPropChipRevision = "chip-revision";
inline constexpr const char* kPropBoardVersion = "board-version";
inline constexpr const char* kPropFeatures = "features";

namespace fcrosec = fuchsia_hardware_google_ec::wire;
struct CommandResult {
  // Status returned by the EC.
  fcrosec::EcStatus status;
  // Output struct.
  std::vector<uint8_t> data;

  template <typename Output>
  Output* GetData() {
    static_assert(!std::is_pointer<Output>::value, "Output type should not be a pointer.");
    static_assert(std::is_pod<Output>::value, "Output type should be POD.");
    if (data.size() < sizeof(Output)) {
      return nullptr;
    }
    return reinterpret_cast<Output*>(data.data());
  }
};

class ChromiumosEcCore;
using DeviceType = ddk::Device<ChromiumosEcCore, ddk::Initializable, ddk::Unbindable>;
class ChromiumosEcCore : public DeviceType {
 public:
  explicit ChromiumosEcCore(zx_device_t* parent)
      : DeviceType(parent),
        loop_(&kAsyncLoopConfigNeverAttachToThread),
        executor_(loop_.dispatcher()) {}
  virtual ~ChromiumosEcCore() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // For inspect test.
  inspect::Inspector& inspect() { return inspect_; }

  // Issue an EC command with the given input.
  template <typename Input>
  fpromise::promise<CommandResult, zx_status_t> IssueCommand(uint16_t command, uint8_t version,
                                                             Input& input) {
    static_assert(!std::is_pointer<Input>::value, "Input type should be a reference, not pointer.");
    static_assert(std::is_pod<Input>::value, "Input type should be POD.");
    return IssueRawCommand(command, version,
                           cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&input), sizeof(Input)));
  }
  // Issue an EC command with no input.
  fpromise::promise<CommandResult, zx_status_t> IssueCommand(uint16_t command, uint8_t version) {
    return IssueRawCommand(command, version, cpp20::span<uint8_t>());
  }

  // Returns true if |feature| is supported by the EC.
  bool HasFeature(size_t feature);
  async::Executor& executor() { return executor_; }
  async::Loop& loop() { return loop_; }

  fidl::WireSharedClient<fuchsia_hardware_acpi::Device>& acpi() { return acpi_client_; }

 private:
  fpromise::promise<CommandResult, zx_status_t> IssueRawCommand(uint16_t command, uint8_t version,
                                                                cpp20::span<uint8_t> input);

  // Bind the FIDL client on the async dispatcher thread, which is necessary to obey the threading
  // constraints of fidl::WireClient.
  fpromise::promise<> BindFidlClients(fidl::ClientEnd<fuchsia_hardware_google_ec::Device> ec_client,
                                      fidl::ClientEnd<fuchsia_hardware_acpi::Device> acpi_client);

  // Run commands for populating inspect data.
  void ScheduleInspectCommands();

  inspect::Inspector inspect_;
  inspect::Node core_ = inspect_.GetRoot().CreateChild(kNodeCore);
  async::Loop loop_;
  async::Executor executor_;
  fidl::WireSharedClient<fuchsia_hardware_acpi::Device> acpi_client_;
  fidl::WireSharedClient<fuchsia_hardware_google_ec::Device> ec_client_;
  fpromise::bridge<> acpi_teardown_;
  fpromise::bridge<> ec_teardown_;
  std::optional<ddk::InitTxn> init_txn_;

  ec_response_get_features features_;
};

}  // namespace chromiumos_ec_core

#endif  // SRC_DEVICES_MCU_DRIVERS_CHROMIUMOS_EC_CORE_CHROMIUMOS_EC_CORE_H_
