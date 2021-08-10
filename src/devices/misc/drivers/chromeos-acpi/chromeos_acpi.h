// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_CHROMEOS_ACPI_CHROMEOS_ACPI_H_
#define SRC_DEVICES_MISC_DRIVERS_CHROMEOS_ACPI_CHROMEOS_ACPI_H_

#include <fuchsia/acpi/chromeos/llcpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>

#include <unordered_set>

#include <ddktl/device.h>

#include "src/devices/lib/acpi/client.h"

namespace chromeos_acpi {

constexpr char kHwidMethodName[] = "HWID";
constexpr char kRoFirmwareMethodName[] = "FRID";
constexpr char kRwFirmwareMethodName[] = "FWID";
constexpr char kNvramLocationMethodName[] = "VBNV";
constexpr char kFlashmapBaseMethodName[] = "FMAP";

namespace facpi = fuchsia_hardware_acpi::wire;

class ChromeosAcpi;
using DeviceType = ddk::Device<ChromeosAcpi, ddk::Initializable,
                               ddk::Messageable<fuchsia_acpi_chromeos::Device>::Mixin>;
class ChromeosAcpi : public DeviceType {
 public:
  ChromeosAcpi(zx_device_t* parent, acpi::Client acpi)
      : DeviceType(parent), acpi_(std::move(acpi)) {}
  ~ChromeosAcpi() override = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // FIDL method implementations.
  void GetHardwareId(GetHardwareIdRequestView request,
                     GetHardwareIdCompleter::Sync& completer) override;
  void GetRwFirmwareVersion(GetRwFirmwareVersionRequestView request,
                            GetRwFirmwareVersionCompleter::Sync& completer) override;
  void GetRoFirmwareVersion(GetRoFirmwareVersionRequestView request,
                            GetRoFirmwareVersionCompleter::Sync& completer) override;
  void GetNvramMetadataLocation(GetNvramMetadataLocationRequestView request,
                                GetNvramMetadataLocationCompleter::Sync& completer) override;
  void GetFlashmapAddress(GetFlashmapAddressRequestView request,
                          GetFlashmapAddressCompleter::Sync& completer) override;
  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

 private:
  struct NvramInfo {
    uint32_t base;
    uint32_t size;
  };
  inspect::Inspector inspect_;
  acpi::Client acpi_;
  std::unordered_set<std::string> available_methods_;
  std::optional<std::string> hwid_;
  std::optional<std::string> ro_fwid_;
  std::optional<std::string> rw_fwid_;
  std::optional<uintptr_t> flashmap_base_;
  std::optional<NvramInfo> nvram_location_;

  std::unordered_set<std::string> ParseMlst(const facpi::Object& object);
  zx_status_t EvaluateObjectHelper(const char* name,
                                   std::function<void(const facpi::Object&)> callback);
};

}  // namespace chromeos_acpi

#endif  // SRC_DEVICES_MISC_DRIVERS_CHROMEOS_ACPI_CHROMEOS_ACPI_H_
