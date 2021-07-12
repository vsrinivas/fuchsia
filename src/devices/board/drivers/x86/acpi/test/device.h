// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_TEST_DEVICE_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_TEST_DEVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <acpica/acpi.h>

#include "src/devices/board/drivers/x86/acpi/status.h"
#include "src/devices/board/drivers/x86/acpi/util.h"
#include "src/devices/lib/acpi/util.h"
namespace acpi::test {

class Device {
 public:
  explicit Device(std::string name) : name_(std::move(name)) {}

  void SetAdr(uint64_t val) { adr_ = val; }
  void SetHid(std::string hid) { hid_ = std::move(hid); }
  void SetCids(std::initializer_list<std::string> cids) { cids_ = std::vector<std::string>(cids); }
  void SetSta(uint64_t val) { sta_ = val; }
  void AddDsd(const acpi::Uuid& uuid, ACPI_OBJECT value) {
    dsd_.emplace(uuid, std::vector<ACPI_OBJECT>{}).first->second.emplace_back(value);
  }

  // Add a child to this device.
  void AddChild(std::unique_ptr<Device> c) {
    c->parent_ = this;
    children_.emplace_back(std::move(c));
  }

  // Add a resource to this device.
  void AddResource(ACPI_RESOURCE r) { resources_.emplace_back(r); }

  // Find a device by path. This implements the rules specified in the ACPI spec, v6.4, section 5.3,
  // with the exception of searching parents for single-component paths.
  Device* FindByPath(std::string path);

  // Return this device's absolute path.
  std::string GetAbsolutePath();

  const std::vector<std::unique_ptr<Device>>& children() { return children_; }
  const std::vector<ACPI_RESOURCE>& resources() { return resources_; }
  const std::optional<std::string>& hid() { return hid_; }
  std::optional<uint64_t> adr() { return adr_; }
  const std::vector<std::string>& cids() { return cids_; }
  const std::unordered_map<acpi::Uuid, std::vector<ACPI_OBJECT>>& dsd() { return dsd_; }
  uint64_t sta() const { return sta_.value_or(~0); }

  // Equivalent of AcpiEvaluateObject.
  acpi::status<acpi::UniquePtr<ACPI_OBJECT>> EvaluateObject(
      std::string pathname, std::optional<std::vector<ACPI_OBJECT>> args);

  // ACPI names are all four characters long.
  // In practice this means that they're represented as uint32_t where each byte
  // corresponds to a letter. Names less than four characters long are padded with '_'.
  //
  // This function takes the name_ of a device and returns one of the "fourcc" codes described
  // above.
  // https://en.wikipedia.org/wiki/FourCC
  uint32_t fourcc_name() {
    ZX_ASSERT(name_.size() <= 4);
    // start with all underscores.
    uint32_t result = 0x5f5f5f5f;
    uint32_t shift = 0;
    for (char i : name_) {
      result &= ~(0xff << shift);
      result |= (i << shift);
      shift += 8;
    }
    return result;
  }

  ACPI_HANDLE parent() { return parent_; }

 private:
  Device* FindByPathInternal(std::string path);
  Device* LookupChild(std::string_view name);

  std::vector<ACPI_RESOURCE> resources_;
  std::vector<std::unique_ptr<Device>> children_;
  Device* parent_ = nullptr;
  std::string name_;
  std::optional<uint64_t> adr_;
  std::optional<std::string> hid_;
  std::vector<std::string> cids_;
  std::optional<uint64_t> sta_;

  // _DSD, map of uuid to values.
  std::unordered_map<acpi::Uuid, std::vector<ACPI_OBJECT>> dsd_;
};

}  // namespace acpi::test
#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_ACPI_TEST_DEVICE_H_
