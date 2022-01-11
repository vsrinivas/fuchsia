// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.google.ec/cpp/markers.h>
#include <fidl/fuchsia.hardware.google.ec/cpp/wire_test_base.h>
#include <lib/async/dispatcher.h>
#include <lib/inspect/testing/cpp/zxtest/inspect.h>

#include <unordered_map>

#include <chromiumos-platform-ec/ec_commands.h>

#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/mcu/drivers/chromiumos-ec-core/chromiumos_ec_core.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
namespace chromiumos_ec_core {

template <typename T>
fidl::VectorView<uint8_t> MakeVectorView(T& response) {
  static_assert(std::is_pod<T>::value, "MakeVectorView must use plain old data type");

  return fidl::VectorView<uint8_t>::FromExternal(reinterpret_cast<uint8_t*>(&response), sizeof(T));
}

class FakeEcDevice : public fuchsia_hardware_google_ec::testing::Device_TestBase {
 public:
  void SetFeatures(std::initializer_list<uint32_t> features) {
    for (uint32_t feature : features) {
      if (feature < 32) {
        features_.flags[0] |= EC_FEATURE_MASK_0(feature);
      } else {
        features_.flags[1] |= EC_FEATURE_MASK_1(feature);
      }
    }
  }

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override;
  void RunCommand(RunCommandRequestView request, RunCommandCompleter::Sync& completer) override;

  using CommandHandler =
      std::function<void(const void* data, size_t data_size, RunCommandCompleter::Sync& completer)>;
  void AddCommand(uint16_t command, uint16_t version, CommandHandler handler) {
    commands_.emplace(MakeKey(command, version), std::move(handler));
  }

 private:
  static uint32_t MakeKey(uint16_t command, uint16_t version) { return (command << 16) | version; }
  std::unordered_map<uint32_t, CommandHandler> commands_;
  ec_response_get_features features_;
};

class ChromiumosEcTestBase : public inspect::InspectTestHelper, public zxtest::Test {
 public:
  void SetUp() override;
  void InitDevice();

  void TearDown() override;

 protected:
  std::shared_ptr<zx_device> fake_root_;
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
  FakeEcDevice fake_ec_;
  acpi::mock::Device fake_acpi_;
  ChromiumosEcCore* device_;

  sync_completion_t ec_shutdown_;
  sync_completion_t acpi_shutdown_;
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_google_ec::Device>> ec_binding_;
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_acpi::Device>> acpi_binding_;
  fidl::ClientEnd<fuchsia_hardware_acpi::NotifyHandler> handler_;

  bool initialised_ = false;
};

}  // namespace chromiumos_ec_core
