// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-test-device-controller.h"

namespace {

constexpr int kI2cRetries = 10;

}  // namespace

namespace sdmmc {

zx::status<std::vector<uint8_t>> SdmmcTestDeviceController::ReadI2c(
    const std::vector<uint8_t>& address, const uint8_t size) {
  fidl::Arena allocator;

  fidl::WireRequest<fuchsia_hardware_i2c::Device2::Transfer> request;

  request.segments_is_write = fidl::VectorView<bool>(allocator, 2);
  request.segments_is_write[0] = true;
  request.segments_is_write[1] = false;

  request.write_segments_data = fidl::VectorView<fidl::VectorView<uint8_t>>(allocator, 1);
  request.write_segments_data[0] = fidl::VectorView<uint8_t>(allocator, address.size());
  memcpy(request.write_segments_data[0].mutable_data(), address.data(),
         address.size() * sizeof(address[0]));

  request.read_segments_length = fidl::VectorView<uint8_t>(allocator, 1);
  request.read_segments_length[0] = size;

  return RetryI2cRequest(request);
}

zx::status<> SdmmcTestDeviceController::WriteI2c(const std::vector<uint8_t>& address,
                                                 const std::vector<uint8_t>& data) {
  if (address.size() + data.size() > UINT8_MAX) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  fidl::Arena allocator;

  fidl::WireRequest<fuchsia_hardware_i2c::Device2::Transfer> request;

  request.segments_is_write = fidl::VectorView<bool>(allocator, 1);
  request.segments_is_write[0] = true;

  request.write_segments_data = fidl::VectorView<fidl::VectorView<uint8_t>>(allocator, 1);
  request.write_segments_data[0] =
      fidl::VectorView<uint8_t>(allocator, address.size() + data.size());

  {
    uint8_t* const write_data = request.write_segments_data[0].mutable_data();
    memcpy(write_data, address.data(), address.size() * sizeof(address[0]));
    memcpy(write_data + address.size(), data.data(), data.size() * sizeof(data[0]));
  }

  request.read_segments_length = fidl::VectorView<uint8_t>(allocator, 0);

  if (const auto status = RetryI2cRequest(request); status.is_error()) {
    return zx::error(status.error_value());
  }
  return zx::ok();
}

zx::status<std::vector<uint8_t>> SdmmcTestDeviceController::ReadReg(const uint8_t reg,
                                                                    const uint8_t size) {
  return ReadI2c({reg}, size);
}

zx::status<uint8_t> SdmmcTestDeviceController::ReadReg(const uint8_t reg) {
  const zx::status<std::vector<uint8_t>> read_data = ReadI2c({reg}, 1);
  if (read_data.is_error()) {
    return zx::error(read_data.error_value());
  }
  return zx::ok((*read_data)[0]);
}

zx::status<> SdmmcTestDeviceController::WriteReg(const uint8_t reg, const uint8_t value) {
  return WriteI2c({reg}, {value});
}

zx::status<std::vector<uint8_t>> SdmmcTestDeviceController::ReadFunction(const uint8_t function,
                                                                         const uint32_t address,
                                                                         const uint8_t size) {
  if (address > kMaxFunctionAddress || function > 7) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  return ReadI2c(FunctionAddressToVector(function, address), size);
}

zx::status<uint8_t> SdmmcTestDeviceController::ReadFunction(const uint8_t function,
                                                            const uint32_t address) {
  const zx::status<std::vector<uint8_t>> read_data = ReadFunction(function, address, 1);
  if (read_data.is_error()) {
    return zx::error(read_data.error_value());
  }
  return zx::ok((*read_data)[0]);
}

zx::status<> SdmmcTestDeviceController::WriteFunction(const uint8_t function,
                                                      const uint32_t address,
                                                      const std::vector<uint8_t>& data) {
  if (address > kMaxFunctionAddress || function > 7) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  return WriteI2c(FunctionAddressToVector(function, address), data);
}

zx::status<std::array<uint8_t, 4>> SdmmcTestDeviceController::GetId() {
  std::array<uint8_t, 4> id;
  // TODO: Register address constant
  zx::status<std::vector<uint8_t>> id_vector = ReadReg(1, id.size());
  if (id_vector.is_error()) {
    return zx::error(id_vector.status_value());
  }
  if (id_vector->size() != id.size()) {
    fprintf(stderr, "Unexpected read data size %lu\n", id_vector->size());
    return zx::error(ZX_ERR_INTERNAL);
  }

  memcpy(id.data(), id_vector->data(), id.size());
  return zx::ok(id);
}

std::vector<uint8_t> SdmmcTestDeviceController::FunctionAddressToVector(const uint8_t function,
                                                                        const uint32_t address) {
  std::vector<uint8_t> ret(4);
  ret[0] = 0xf0 | function;
  ret[1] = (address >> 16) & 0xff;
  ret[2] = (address >> 8) & 0xff;
  ret[3] = address & 0xff;
  return ret;
}

zx::status<std::vector<uint8_t>> SdmmcTestDeviceController::RetryI2cRequest(
    const fidl::WireRequest<fuchsia_hardware_i2c::Device2::Transfer>& request) {
  for (int i = 0; i < kI2cRetries; i++) {
    fidl::WireRequest<fuchsia_hardware_i2c::Device2::Transfer> req = request;
    const auto response =
        fidl::WireResult<fuchsia_hardware_i2c::Device2::Transfer>(i2c_.client_end(), &req);
    if (!response.ok()) {
      fprintf(stderr, "FIDL request failed: %s\n", zx_status_get_string(response.status()));
      return zx::error(response.status());
    }

    // An error here represents an I2C bus error, continue to retry the transfer.
    if (response->result.is_err()) {
      continue;
    }

    if (response->result.response().read_segments_data.count() !=
        request.read_segments_length.count()) {
      fprintf(stderr, "Invalid read segments count %lu\n",
              response->result.response().read_segments_data.count());
      return zx::error(ZX_ERR_INTERNAL);
    }

    // Write request -- no data to return.
    if (request.read_segments_length.count() == 0) {
      return zx::ok(std::vector<uint8_t>{});
    }

    const fidl::VectorView<uint8_t>& read_data = response->result.response().read_segments_data[0];
    if (read_data.count() != request.read_segments_length[0]) {
      fprintf(stderr, "Unexpected read data size %lu\n", read_data.count());
      return zx::error(ZX_ERR_INTERNAL);
    }

    return zx::ok(std::vector<uint8_t>(read_data.data(), read_data.data() + read_data.count()));
  }

  return zx::error(ZX_ERR_IO);
}

}  // namespace sdmmc
