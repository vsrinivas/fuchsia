// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libgen.h>
#include <limits.h>

#include <list>
#include <unordered_set>

#include <acpica/acpi.h>
#include <gtest/gtest.h>

#include "src/devices/board/lib/acpi/acpi-impl.h"
#include "src/devices/board/lib/acpi/device-args.h"
#include "src/devices/board/lib/acpi/device-for-host.h"
#include "src/devices/board/lib/acpi/manager-host.h"
#include "src/devices/board/tests/acpi-host-tests/table-manager.h"
#include "third_party/acpica/source/include/actypes.h"

// Path to the compiled tables in out/, passed in argv[1].
std::string test_data_dir;
namespace acpi_host_test {

class AcpiHostTest : public testing::Test {
 public:
  void InitAcpiWithTables(const char* table_name) {
    auto* tables = acpi::AcpiTableManager::LoadFromDir(test_data_dir + "/" + table_name);
    ASSERT_NE(nullptr, tables);
    tables->ApplyFixups();

    ASSERT_EQ(AE_OK, AcpiInitializeSubsystem());
    AcpiInstallAddressSpaceHandler(ACPI_ROOT_OBJECT, ACPI_ADR_SPACE_SYSTEM_MEMORY,
                                   AcpiHostTest::MemoryHandlerThunk, nullptr, this);

    ASSERT_EQ(AE_OK, AcpiInitializeTables(NULL, 32, FALSE));
    ASSERT_EQ(AE_OK, AcpiLoadTables());

    ASSERT_EQ(AE_OK, AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION));

    ASSERT_EQ(AE_OK, AcpiInitializeObjects(ACPI_FULL_INITIALIZATION));
  }

  void MemoryHandler(uint32_t func, ACPI_PHYSICAL_ADDRESS addr, uint32_t width, uint64_t* value) {
    if (func == ACPI_READ) {
      uint8_t* value_bytes = reinterpret_cast<uint8_t*>(value);
      for (size_t i = 0; i < width; i++) {
        auto entry = fake_mmio_.find(addr + i);
        if (entry == fake_mmio_.end()) {
          continue;
        }

        value_bytes[i] = entry->second;
      }
    } else {
      uint8_t* value_bytes = reinterpret_cast<uint8_t*>(value);
      for (size_t i = 0; i < width; i++) {
        fake_mmio_.emplace(addr + i, value_bytes[i]);
      }
    }
  }

  static ACPI_STATUS MemoryHandlerThunk(uint32_t func, ACPI_PHYSICAL_ADDRESS addr, uint32_t width,
                                        uint64_t* value, void* ctx, void* region_ctx) {
    static_cast<AcpiHostTest*>(ctx)->MemoryHandler(func, addr, width, value);
    return AE_OK;
  }

  void TearDown() override {
    // Normally the DDK would free things, but we don't have the DDK, so we have to do it ourselves.
    for (auto value : manager_.zx_devices_) {
      auto dev = static_cast<acpi::Device*>(value.second);
      delete dev;
    }
  }

 protected:
  acpi::AcpiImpl acpi_;
  acpi::Device root_device_{acpi::DeviceArgs(nullptr, &manager_, ACPI_ROOT_OBJECT)};
  acpi::HostManager manager_{&acpi_, root_device_.zxdev()};
  std::unordered_map<ACPI_PHYSICAL_ADDRESS, uint8_t> fake_mmio_;
};

TEST_F(AcpiHostTest, DeviceIsChildOfScopeTest) {
  InitAcpiWithTables("device-child-of-scope");

  ASSERT_EQ(AE_OK, manager_.DiscoverDevices().status_value());
  ASSERT_EQ(AE_OK, manager_.ConfigureDiscoveredDevices().status_value());
  ASSERT_EQ(AE_OK, manager_.PublishDevices(nullptr).status_value());

  auto root_hnd = manager_.acpi()->GetHandle(nullptr, "\\");
  ASSERT_EQ(AE_OK, root_hnd.status_value());
  auto root = manager_.LookupDevice(*root_hnd);

  auto hnd = manager_.acpi()->GetHandle(ACPI_ROOT_OBJECT, "_GPE.DEV0");
  ASSERT_EQ(AE_OK, hnd.status_value());

  auto child = manager_.LookupDevice(*hnd);
  ASSERT_NE(nullptr, child);
  ASSERT_TRUE(child->built());
  ASSERT_EQ(root, child->parent())
      << "Child of scope should end up as a child of the nearest ancestor device";
}
}  // namespace acpi_host_test

zx_status_t pci_init(zx_device_t* parent, ACPI_HANDLE object,
                     acpi::UniquePtr<ACPI_DEVICE_INFO> info, acpi::Manager* acpi,
                     std::vector<pci_bdf_t> acpi_bdfs) {
  return ZX_OK;
}

int main(int argc, char** argv) {
  if (argc != 2) {
    printf("Usage: %s <path/to/tables>\n", argv[0]);
    return -1;
  }
  test_data_dir = argv[1];

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
