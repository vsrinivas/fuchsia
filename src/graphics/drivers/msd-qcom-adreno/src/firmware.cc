// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "firmware.h"

const char* Firmware::GetFirmwareFilename(uint32_t chip_id) {
  // chip_id is 0xAABBCCDD where AA is core, BB is major, CC is minor, DD is revision
  switch (chip_id >> 16) {
    case 0x0603:
      return
#ifdef __linux__
          "/lib/firmware/qcom/"
#endif
          "a630_sqe.fw";
    default:
      return DRETP(nullptr, "Firmware unknown for chip_id 0x%x", chip_id);
  }
}

std::unique_ptr<Firmware> Firmware::Create(MsdQcomPlatformDevice* device) {
  auto firmware = std::make_unique<Firmware>();
  if (!firmware->Init(device))
    return DRETP(nullptr, "Failed to Init firmware");
  return firmware;
}

bool Firmware::Map(std::shared_ptr<AddressSpace> address_space) {
  gpu_mapping_ = address_space->MapBufferGpu(address_space, buffer_);
  if (!gpu_mapping_)
    return DRETF(false, "MapBufferGpu failed");
  return true;
}

bool Firmware::Init(MsdQcomPlatformDevice* device) {
  const char* filename = GetFirmwareFilename(device->GetChipId());
  if (!filename)
    return DRETF(false, "Couldn't get firmware filename");

  std::unique_ptr<magma::PlatformBuffer> buffer;
  magma::Status status = device->platform_device()->LoadFirmware(filename, &buffer, &size_);
  if (!status) {
    buffer_.reset();
    return DRETF(false, "LoadFirmware failed: %d", status.get());
  }

  {
    // Remove header
    void* data;
    if (!buffer->MapCpu(&data))
      return DRETF(false, "Couldn't map firmware buffer");

    DASSERT(size_ > sizeof(uint32_t));
    size_ -= sizeof(uint32_t);
    memcpy(data, reinterpret_cast<uint8_t*>(data) + sizeof(uint32_t), size_);

    buffer->UnmapCpu();
  }

  buffer_ = std::move(buffer);
  return true;
}
