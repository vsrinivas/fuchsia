// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDMMC_HARDWARE_TEST_SDMMC_TEST_DEVICE_CONTROLLER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDMMC_HARDWARE_TEST_SDMMC_TEST_DEVICE_CONTROLLER_H_

#include <fcntl.h>
#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/status.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <array>

#include <fbl/unique_fd.h>

namespace sdmmc {

template <typename T>
fidl::WireSyncClient<T> GetFidlClient(fbl::unique_fd device) {
  fidl::ClientEnd<T> client;
  zx_status_t status =
      fdio_get_service_handle(device.release(), client.channel().reset_and_get_address());
  if (status != ZX_OK) {
    return {};
  }

  return fidl::WireSyncClient<T>(std::move(client));
}

template <typename T>
fidl::WireSyncClient<T> GetFidlClient(const char* path) {
  fbl::unique_fd device(open(path, O_RDWR));
  if (!device.is_valid()) {
    return {};
  }

  return GetFidlClient<T>(std::move(device));
}

class SdmmcTestDeviceController {
 public:
  SdmmcTestDeviceController() = default;
  explicit SdmmcTestDeviceController(fidl::WireSyncClient<fuchsia_hardware_i2c::Device2> i2c)
      : i2c_(std::move(i2c)) {}

  SdmmcTestDeviceController(SdmmcTestDeviceController&& other) noexcept
      : i2c_(std::move(other.i2c_)) {}
  SdmmcTestDeviceController& operator=(SdmmcTestDeviceController&& other) noexcept {
    i2c_ = std::move(other.i2c_);
    return *this;
  }

  bool is_valid() const { return i2c_.is_valid(); }

  zx::status<std::vector<uint8_t>> ReadI2c(const std::vector<uint8_t>& address, uint8_t size);
  zx::status<> WriteI2c(const std::vector<uint8_t>& address, const std::vector<uint8_t>& data);

  zx::status<std::vector<uint8_t>> ReadReg(uint8_t reg, uint8_t size);
  zx::status<uint8_t> ReadReg(uint8_t reg);
  zx::status<> WriteReg(uint8_t reg, uint8_t value);

  zx::status<std::vector<uint8_t>> ReadFunction(uint8_t function, uint32_t address, uint8_t size);
  zx::status<uint8_t> ReadFunction(uint8_t function, uint32_t address);
  zx::status<> WriteFunction(uint8_t function, uint32_t address, const std::vector<uint8_t>& data);

  // TODO: Register address constant
  zx::status<uint8_t> GetCoreVersion() { return ReadReg(0); }
  zx::status<std::array<uint8_t, 4>> GetId();

 private:
  static constexpr uint32_t kMaxFunctionAddress = 0x1'ffff;

  static std::vector<uint8_t> FunctionAddressToVector(uint8_t function, uint32_t address);

  zx::status<std::vector<uint8_t>> RetryI2cRequest(
      const fidl::WireRequest<fuchsia_hardware_i2c::Device2::Transfer>& request);

  fidl::WireSyncClient<fuchsia_hardware_i2c::Device2> i2c_;
};

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDMMC_HARDWARE_TEST_SDMMC_TEST_DEVICE_CONTROLLER_H_
