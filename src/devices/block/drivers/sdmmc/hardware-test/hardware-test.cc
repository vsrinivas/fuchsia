// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <fidl/fuchsia.hardware.sdio/cpp/wire.h>
#include <fuchsia/hardware/sdio/c/banjo.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/llcpp/arena.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <sys/types.h>

#include <vector>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "sdmmc-test-device-controller-regs.h"
#include "sdmmc-test-device-controller.h"

namespace sdmmc {

class SdmmcHardwareTest : public zxtest::Test {
 public:
  void SetUp() override {
    fidl::WireSyncClient i2c =
        GetFidlClient<fuchsia_hardware_i2c::Device2>(kControllerI2cDevicePath);
    controller_ = SdmmcTestDeviceController(std::move(i2c));
    ASSERT_TRUE(controller_.is_valid(), "Failed to connect to I2C device");

    // Reset the test device controller. This will probably fail because the controller doesn't ack
    // on I2C before resetting.
    CoreControl::Get().FromValue(0).set_por_reset(1).WriteTo(controller_);

    const zx::time start = zx::clock::get_monotonic();
    while (CoreControl::Get().FromValue(0).ReadFrom(controller_) != ZX_OK) {
    }
    printf("Took %luus for core to reset\n", (zx::clock::get_monotonic() - start).to_usecs());

    EXPECT_OK(CoreControl::Get().FromValue(0).set_core_enable(1).WriteTo(controller_));

    // 0x0000 is reserved, set RCA to 0x0001 instead.
    EXPECT_OK(Rca0::Get().FromValue(1).WriteTo(controller_));
    EXPECT_OK(Rca1::Get().FromValue(0).WriteTo(controller_));

    // Support the entire voltage range.
    EXPECT_OK(Ocr0::Get().FromValue(0).WriteTo(controller_));
    EXPECT_OK(Ocr1::Get().FromValue(0b1111'1111).WriteTo(controller_));
    EXPECT_OK(Ocr2::Get().FromValue(0b1111'1111).WriteTo(controller_));

    SetupCis();
  }

 protected:
  // TODO: Extract these into a device-specific config
  static constexpr char kControllerI2cDevicePath[] =
      "/dev/sys/platform/05:00:2/aml-i2c/i2c/i2c-1-50";
  static constexpr char kSdmmcDevicePath[] = "/dev/aml_sd";

  void SetupCis() {
    // CCCR
    EXPECT_OK(controller_.WriteFunction(
        0, 0x0000,
        {
            // clang-format disable
            /* [000] */ 0x43, 0x03, 0x00, 0x06,  // SDIO spec 3.00, functions 1 and 2 ready
            /* [004] */ 0x00, 0x00, 0x00, 0x00,
            // ----- BUG ----- Must set 4BLS for core driver to register four-bit bus capability.
            /* [008] */ 0x83, 0x00, 0x10, 0x00,  // cmd52/cmd53 supported, CIS pointer (0x1000)
            /* [00c] */ 0x00, 0x00, 0x00, 0x00,
            /* [010] */ 0x00, 0x00, 0x00, 0x01,  // No function 0 block ops, high speed supported
            /* [014] */ 0x07, 0x00, 0x00, 0x00,  // SDR50/SDR104/DDR50 supported
            // clang-format enable
        }));

    // FBR function 1
    EXPECT_OK(controller_.WriteFunction(
        0, 0x0100,
        {
            // clang-format disable
            /* [100] */ 0x00, 0x00, 0x00, 0x00,
            /* [104] */ 0x00, 0x00, 0x00, 0x00,
            /* [108] */ 0x00, 0x0d, 0x10, 0x00,  // CIS pointer (0x100d)
            /* [10c] */ 0x00, 0x00, 0x00, 0x00,
            /* [110] */ 0x00, 0x00, 0x00, 0x00,  // Block size set to zero initially
            // clang-format enable
        }));

    // FBR function 2
    EXPECT_OK(controller_.WriteFunction(
        0, 0x0200,
        {
            // clang-format disable
            /* [200] */ 0x00, 0x00, 0x00, 0x00,
            /* [204] */ 0x00, 0x00, 0x00, 0x00,
            /* [208] */ 0x00, 0x0d, 0x10, 0x00,  // CIS pointer (0x100d)
            /* [20c] */ 0x00, 0x00, 0x00, 0x00,
            /* [210] */ 0x00, 0x00, 0x00, 0x00,  // Block size set to zero initially
            // clang-format enable
        }));

    EXPECT_OK(controller_.WriteFunction(
        0, 0x1000,
        {
            // clang-format disable
            // CISTPL_FUNCE for function 0: 4 bytes, block size 256, max transfer rate 200Mbit/s.
            0x22, 0x04, 0x00, 0x00, 0x01, 0b101'011,
            // CISTPL_MANFID for function 0: 4 bytes, manufacturer ID 0x0000, product ID 0x0000.
            0x20, 0x04, 0x00, 0x00, 0x00, 0x00,
            // CISTPL_END
            0xff,
            // CISTPL_FUNCE for I/O functions: 42 bytes, block size 512.
            0x22, 0x2a, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00,
            // CISTPL_MANFID for function 0: 4 bytes, manufacturer ID 0x0000, product ID 0x0000.
            0x20, 0x04, 0x00, 0x00, 0x00, 0x00,
            // CISTPL_END
            0xff,
            // clang-format enable
        }));
  }

  static void RestartSdmmcDriver() {
    fidl::WireSyncClient sdmmc_device = GetFidlClient<fuchsia_device::Controller>(kSdmmcDevicePath);
    ASSERT_TRUE(sdmmc_device.is_valid());

    const auto response = sdmmc_device->Rebind(fidl::StringView());
    ASSERT_TRUE(response.ok());
    EXPECT_TRUE(response->result.is_response());
  }

  std::vector<fidl::WireSyncClient<fuchsia_hardware_sdio::Device>> GetTestSdioFidlClients(
      const uint8_t max_function) {
    if (max_function == 0 || max_function > 7) {
      return {};
    }

    std::vector<fidl::WireSyncClient<fuchsia_hardware_sdio::Device>> clients(max_function);
    for (uint8_t i = 1; i <= max_function; i++) {
      // Loop indefinitely trying to get the client. We have to wait for SDIO intialization to
      // complete for the devices to be created, which may take some time. If initialization errors
      // occur let the test runner time out and fail the test.
      while (!clients[i - 1].is_valid()) {
        clients[i - 1] = GetTestSdioFidlClient(i);
      }
    }

    return clients;
  }

  SdmmcTestDeviceController controller_;

 private:
  // Attempts to get a client of the specified function device for the SDIO test rig. Returns an
  // invalid client on any errors.
  static fidl::WireSyncClient<fuchsia_hardware_sdio::Device> GetTestSdioFidlClient(
      const uint8_t function) {
#if 0
    constexpr uint32_t kTestManufacturerId = 0x02d0;
    constexpr uint32_t kTestProductId = 0x4359;
#endif
    constexpr uint32_t kTestManufacturerId = 0x0000;
    constexpr uint32_t kTestProductId = 0x0000;

    if (function == 0) {
      return {};
    }

    DIR* const sdio_dir = opendir("/dev/class/sdio");
    if (sdio_dir == nullptr) {
      return {};
    }

    fidl::WireSyncClient<fuchsia_hardware_sdio::Device> function_client = {};
    struct dirent* entry = readdir(sdio_dir);
    for (; entry != nullptr; entry = readdir(sdio_dir)) {
      fbl::unique_fd device(openat(dirfd(sdio_dir), entry->d_name, O_RDWR));
      if (!device.is_valid()) {
        continue;
      }

      fidl::WireSyncClient client = GetFidlClient<fuchsia_hardware_sdio::Device>(std::move(device));
      if (!client.is_valid()) {
        continue;
      }

      const auto response = client->GetDevHwInfo();
      if (response->result.is_err()) {
        continue;
      }
      if (response->result.response().function != function) {
        continue;
      }

      const auto& hw_info = response->result.response().hw_info;
      if (hw_info.dev_hw_info.num_funcs <= function) {
        continue;
      }

      const auto& function_info = hw_info.funcs_hw_info[function];
      if (function_info.manufacturer_id == kTestManufacturerId &&
          function_info.product_id == kTestProductId) {
        function_client = std::move(client);
        break;
      }
    }

    closedir(sdio_dir);
    return function_client;
  }
};

TEST_F(SdmmcHardwareTest, InitSuccess) {
  // Re-bind the SDMMC driver to initialize with the new settings.
  printf("Restarting SDMMC driver\n");
  RestartSdmmcDriver();
  printf("Done, waiting for FPGA init success\n");

  EXPECT_OK(CoreStatus::WaitForInitSuccess(controller_));
  printf("Done, waiting for SDIO clients\n");

  auto clients = GetTestSdioFidlClients(2);
  ASSERT_EQ(clients.size(), 2);
  printf("Done\n");

  // ----- BUG ----- Core driver doesn't set SDIO_CARD_DIRECT_COMMAND capability.
  constexpr uint32_t kExpectedSdioCaps =
      SDIO_CARD_MULTI_BLOCK | /* SDIO_CARD_DIRECT_COMMAND | */ SDIO_CARD_HIGH_SPEED |
      SDIO_CARD_FOUR_BIT_BUS | SDIO_CARD_UHS_SDR50 | SDIO_CARD_UHS_SDR104 | SDIO_CARD_UHS_DDR50;

  for (const auto& client : clients) {
    // TODO: Read CCCR and make sure all registers were set correctly
    const auto response = client->GetDevHwInfo();
    ASSERT_TRUE(response.ok());
    ASSERT_TRUE(response->result.is_response());

    const auto& hw_info = response->result.response().hw_info;
    EXPECT_EQ(hw_info.dev_hw_info.num_funcs, 3);  // Includes function 0
    EXPECT_EQ(hw_info.dev_hw_info.sdio_vsn, 4);   // Version 3.00
    EXPECT_EQ(hw_info.dev_hw_info.cccr_vsn, 3);   // Version 3.00
    EXPECT_EQ(hw_info.dev_hw_info.caps, kExpectedSdioCaps);

    for (uint32_t i = 0; i < 3; i++) {
      EXPECT_EQ(hw_info.funcs_hw_info[i].manufacturer_id, 0);
      EXPECT_EQ(hw_info.funcs_hw_info[i].product_id, 0);
      if (i == 0) {
        EXPECT_EQ(hw_info.funcs_hw_info[i].max_blk_size, 256);
        EXPECT_EQ(hw_info.funcs_hw_info[i].max_tran_speed, 200'000);
      } else {
        EXPECT_EQ(hw_info.funcs_hw_info[i].max_blk_size, 512);
      }
      EXPECT_EQ(hw_info.funcs_hw_info[i].fn_intf_code, 0);
    }
  }
}

TEST_F(SdmmcHardwareTest, InitSuccessWithCmd52Retries) {
  auto control = CoreControl::Get().FromValue(0);
  EXPECT_OK(control.ReadFrom(controller_));
  EXPECT_OK(control.set_error_injection_enable(1).WriteTo(controller_));

  EXPECT_OK(CrcErrorControl::Get().FromValue(0).set_cmd52_crc_error_enable(1).WriteTo(controller_));
  EXPECT_OK(
      Cmd52ErrorControl::Get().FromValue(0).set_transfers_until_crc_error(5).WriteTo(controller_));

  // Re-bind the SDMMC driver to initialize with the new settings.
  printf("Restarting SDMMC driver\n");
  RestartSdmmcDriver();
  printf("Done, waiting for FPGA init success\n");

  EXPECT_OK(CoreStatus::WaitForInitSuccess(controller_));
  printf("Done, waiting for SDIO clients\n");

  auto clients = GetTestSdioFidlClients(2);
  ASSERT_EQ(clients.size(), 2);
  printf("Done\n");
}

TEST_F(SdmmcHardwareTest, InitFailureCmd52Errors) {
  auto control = CoreControl::Get().FromValue(0);
  EXPECT_OK(control.ReadFrom(controller_));
  EXPECT_OK(control.set_error_injection_enable(1).WriteTo(controller_));

  EXPECT_OK(CrcErrorControl::Get().FromValue(0).set_cmd52_crc_error_enable(1).WriteTo(controller_));
  EXPECT_OK(Cmd52ErrorControl::Get().FromValue(0).WriteTo(controller_));

  // Re-bind the SDMMC driver to initialize with the new settings.
  printf("Restarting SDMMC driver\n");
  RestartSdmmcDriver();
  printf("Done, waiting for FPGA init failure\n");

  EXPECT_OK(CoreStatus::WaitForInitFailure(controller_));
  printf("Done\n");
}

TEST_F(SdmmcHardwareTest, ReadCccr) {
  // Re-bind the SDMMC driver to initialize with the new settings.
  printf("Restarting SDMMC driver\n");
  RestartSdmmcDriver();
  printf("Done, waiting for FPGA init success\n");

  EXPECT_OK(CoreStatus::WaitForInitSuccess(controller_));
  printf("Done, waiting for SDIO clients\n");

  auto clients = GetTestSdioFidlClients(2);
  ASSERT_EQ(clients.size(), 2);
  printf("Done\n");

  zx::status<uint8_t> io_enable = controller_.ReadFunction(0, 0x2);
  ASSERT_TRUE(io_enable.is_ok());
  EXPECT_EQ(io_enable.value(), 0b110);  // Functions 1 and 2 should be enabled.
}

TEST_F(SdmmcHardwareTest, ReadWriteCccr) {
  // Write a reserved bus width value, and make sure it changes after init.
  EXPECT_OK(controller_.WriteFunction(0, 0x7, {0b01}));

  // Re-bind the SDMMC driver to initialize with the new settings.
  printf("Restarting SDMMC driver\n");
  RestartSdmmcDriver();
  printf("Done, waiting for FPGA init success\n");

  EXPECT_OK(CoreStatus::WaitForInitSuccess(controller_));
  printf("Done, waiting for SDIO clients\n");

  auto clients = GetTestSdioFidlClients(2);
  ASSERT_EQ(clients.size(), 2);
  printf("Done\n");

  zx::status<uint8_t> bus_width = controller_.ReadFunction(0, 0x7);
  ASSERT_TRUE(bus_width.is_ok());
  EXPECT_EQ(bus_width.value(), 0b10);
}

}  // namespace sdmmc
