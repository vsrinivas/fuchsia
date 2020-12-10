// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_firmware.h"

#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <cstring>
#include <limits>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/backplane.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/firmware.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_regs.h"

namespace wlan {
namespace brcmfmac {
namespace {

// The firmware binary contains an override for RAM size, immediately following a fourcc.
constexpr uint32_t kFirmwareRamsizeMagic = 0x534D4152;  // SMAR
constexpr uint32_t kFirmwareRamsizeOffset = 0x6C;

// Duration and interval for waiting for firmware boot.
constexpr zx::duration kFirmwareBootTimeout = zx::sec(2);
constexpr zx::duration kFirmwareBootIteration = zx::msec(50);

// Scratch buffer sizes.
constexpr size_t kDmaD2hScratchBufferSize = 8;
constexpr size_t kDmaD2hRingupdateBufferSize = 1024;

// Default value for GetMaxRxbufpost(), if firmware provides no value.
constexpr int kDefaultMaxRxbufpost = 255;

}  // namespace

// Structure definition for our host/device shared memory block.  Offsets are determined by
// (undocumented) firmware convention.
struct [[gnu::packed]] PcieFirmware::SharedRamInfo {
  uint8_t version;
  uint8_t pad0[1];
  uint16_t flags;
  uint32_t pad1[4];
  uint32_t console_addr;
  uint32_t pad2[2];
  uint16_t pad3[1];
  uint16_t max_rxbufpost;
  uint32_t rx_data_offset;
  uint32_t pad4[1];
  uint32_t d2h_mb_data_addr;
  uint32_t ring_info_addr;
  uint32_t dma_scratch_len;
  uint64_t dma_scratch_addr;
  uint32_t dma_ringupd_len;
  uint64_t dma_ringupd_addr;
};

PcieFirmware::PcieFirmware() = default;

PcieFirmware::~PcieFirmware() = default;

// static
zx_status_t PcieFirmware::Create(Device* device, PcieBuscore* buscore,
                                 std::unique_ptr<PcieFirmware>* out_firmware) {
  zx_status_t status = ZX_OK;

  // Get the firmware and NVRAM binaries.
  const CommonCoreId chip_id = buscore->GetBackplane()->chip_id();
  const uint16_t chip_rev = buscore->GetBackplane()->chip_rev();
  std::string firmware_binary;
  if ((status = GetFirmwareBinary(device, brcmf_bus_type::BRCMF_BUS_TYPE_PCIE, chip_id, chip_rev,
                                  &firmware_binary)) != ZX_OK) {
    return status;
  }
  std::string nvram_binary;
  if ((status = GetNvramBinary(device, brcmf_bus_type::BRCMF_BUS_TYPE_PCIE, chip_id, chip_rev,
                               &nvram_binary)) != ZX_OK) {
    if (status != ZX_ERR_NOT_FOUND) {
      return status;
    }
    // A missing NVRAM binary is not a fatal error.
    BRCMF_INFO("NVRAM binary not found");
  }
  if (firmware_binary.size() < 4) {
    BRCMF_ERR("Invalid firmware binary size %zu", firmware_binary.size());
    return ZX_ERR_IO_INVALID;
  }
  uint32_t resetintr = 0;
  std::memcpy(&resetintr, firmware_binary.data(), sizeof(resetintr));

  // Create the chipset.
  std::unique_ptr<Chipset> chipset;
  if ((status = Chipset::Create(buscore, buscore->GetBackplane(), &chipset)) != ZX_OK) {
    BRCMF_ERR("Failed to create chipset: %s", zx_status_get_string(status));
    return status;
  }
  const uint32_t rambase = chipset->GetRambase();
  const size_t ramsize = [&]() {
    size_t ramsize = chipset->GetRamsize();
    if (firmware_binary.size() >= kFirmwareRamsizeOffset + 8) {
      if (std::memcmp(firmware_binary.data() + kFirmwareRamsizeOffset, &kFirmwareRamsizeMagic,
                      sizeof(kFirmwareRamsizeMagic)) == 0) {
        // This firmware binary configures a particular ramsize, so we use that instead.
        std::memcpy(&ramsize,
                    firmware_binary.data() + kFirmwareRamsizeOffset + sizeof(kFirmwareRamsizeMagic),
                    sizeof(ramsize));
      }
    }
    return ramsize;
  }();

  // Upload the firmware.
  if ((status = chipset->EnterUploadState()) != ZX_OK) {
    BRCMF_ERR("Failed to enter upload state: %s", zx_status_get_string(status));
    return status;
  }
  if ((status = buscore->TcmWrite(rambase, firmware_binary.data(), firmware_binary.size())) !=
      ZX_OK) {
    BRCMF_ERR("Failed to write firmware binary: %s", zx_status_get_string(status));
    return status;
  }

  // Clear the last 4 bytes of RAM.  This will flag when the FW is running.
  uint32_t sharedram_addr_value = 0;
  const uint32_t sharedram_addr_offset = ramsize - sizeof(sharedram_addr_value);
  if ((status = buscore->TcmWrite(rambase + sharedram_addr_offset, sharedram_addr_value)) !=
      ZX_OK) {
    BRCMF_ERR("Failed to write firmware flag: %s", zx_status_get_string(status));
    return status;
  }

  // Upload the NVRAM.
  if (!nvram_binary.empty()) {
    if (nvram_binary.size() > ramsize) {
      BRCMF_ERR("NVRAM binary size %zu too large for ramsize %zu", nvram_binary.size(), ramsize);
      return ZX_ERR_IO_INVALID;
    }
    const auto nvram_offset = static_cast<uint32_t>(ramsize - nvram_binary.size());
    buscore->TcmWrite(rambase + nvram_offset, nvram_binary.data(), nvram_binary.size());
  }

  if ((status = buscore->TcmRead(rambase + sharedram_addr_offset, &sharedram_addr_value)) !=
      ZX_OK) {
    BRCMF_ERR("Failed to read firmware flag: %s", zx_status_get_string(status));
    return status;
  }
  if ((status = chipset->ExitUploadState()) != ZX_OK) {
    BRCMF_ERR("Failed to exit upload state: %s", zx_status_get_string(status));
    return status;
  }

  // Activate the chip!
  if ((status = buscore->TcmWrite(0, resetintr)) != ZX_OK) {
    BRCMF_ERR("Failed to write reset interrupt: %s", zx_status_get_string(status));
    return status;
  }
  if ((status = chipset->Reset()) != ZX_OK) {
    BRCMF_ERR("Failed to reset chipset: %s", zx_status_get_string(status));
    return status;
  }

  // Wait for firmware init.
  int poll_iteration = 0;
  while (true) {
    uint32_t new_sharedram_addr_value = 0;
    if ((status = buscore->TcmRead(rambase + sharedram_addr_offset, &new_sharedram_addr_value)) !=
        ZX_OK) {
      BRCMF_ERR("Failed to read firmware flag: %s", zx_status_get_string(status));
      return status;
    }
    if (new_sharedram_addr_value != sharedram_addr_value) {
      sharedram_addr_value = new_sharedram_addr_value;
      break;
    }
    ++poll_iteration;
    if (poll_iteration >= kFirmwareBootTimeout / kFirmwareBootIteration) {
      BRCMF_ERR("Firmware init timed out");
      return ZX_ERR_TIMED_OUT;
    }
    zx::nanosleep(zx::deadline_after(kFirmwareBootIteration));
  }

  // Create the scratch and ringupdate buffers.
  std::unique_ptr<DmaBuffer> dma_d2h_scratch_buffer;
  if ((status = buscore->CreateDmaBuffer(ZX_CACHE_POLICY_CACHED, kDmaD2hScratchBufferSize,
                                         &dma_d2h_scratch_buffer)) != ZX_OK) {
    BRCMF_ERR("Failed to create D2H scratch buffer: %s", zx_status_get_string(status));
    return status;
  }
  std::unique_ptr<DmaBuffer> dma_d2h_ringupdate_buffer;
  if ((status = buscore->CreateDmaBuffer(ZX_CACHE_POLICY_CACHED, kDmaD2hRingupdateBufferSize,
                                         &dma_d2h_ringupdate_buffer)) != ZX_OK) {
    BRCMF_ERR("Failed to create D2H ringupdate buffer: %s", zx_status_get_string(status));
    return status;
  }

  // Setup the shared ram info.
  auto shared_ram_info = std::make_unique<SharedRamInfo>();
  buscore->TcmRead(sharedram_addr_value, shared_ram_info.get(), sizeof(*shared_ram_info));
  if (dma_d2h_scratch_buffer->size() >
      std::numeric_limits<decltype(shared_ram_info->dma_scratch_len)>::max()) {
    BRCMF_ERR("D2H scratch buffer size %zu too large", dma_d2h_scratch_buffer->size());
    return ZX_ERR_INTERNAL;
  }
  shared_ram_info->dma_scratch_len =
      static_cast<decltype(shared_ram_info->dma_scratch_len)>(dma_d2h_scratch_buffer->size());
  if ((status = buscore->TcmRead(sharedram_addr_value, shared_ram_info.get(),
                                 sizeof(*shared_ram_info))) != ZX_OK) {
    BRCMF_ERR("Failed to read shared ram info: %s", zx_status_get_string(status));
    return status;
  }
  shared_ram_info->dma_scratch_len = dma_d2h_scratch_buffer->size();
  shared_ram_info->dma_scratch_addr = dma_d2h_scratch_buffer->dma_address();
  if (dma_d2h_ringupdate_buffer->size() >
      std::numeric_limits<decltype(shared_ram_info->dma_ringupd_len)>::max()) {
    BRCMF_ERR("D2H ringupdate buffer size %zu too large", dma_d2h_ringupdate_buffer->size());
    return ZX_ERR_INTERNAL;
  }
  shared_ram_info->dma_ringupd_len =
      static_cast<decltype(shared_ram_info->dma_ringupd_len)>(dma_d2h_ringupdate_buffer->size());
  shared_ram_info->dma_ringupd_addr = dma_d2h_ringupdate_buffer->dma_address();
  if ((status = buscore->TcmWrite(sharedram_addr_value, shared_ram_info.get(),
                                  sizeof(*shared_ram_info))) != ZX_OK) {
    BRCMF_ERR("Failed to write shared ram info: %s", zx_status_get_string(status));
    return status;
  }

  auto firmware = std::make_unique<PcieFirmware>();
  firmware->buscore_ = buscore;
  firmware->shared_ram_info_ = std::move(shared_ram_info);
  firmware->dma_d2h_scratch_buffer_ = std::move(dma_d2h_scratch_buffer);
  firmware->dma_d2h_ringupdate_buffer_ = std::move(dma_d2h_ringupdate_buffer);

  // Setup the firmware console offsets.
  if ((status = buscore->TcmRead<uint32_t>(
           firmware->shared_ram_info_->console_addr + BRCMF_CONSOLE_BUFADDR_OFFSET,
           &firmware->console_buffer_addr_)) != ZX_OK) {
    BRCMF_ERR("Failed to read console buffer address: %s", zx_status_get_string(status));
    return status;
  }
  if ((status = buscore->TcmRead<uint32_t>(
           firmware->shared_ram_info_->console_addr + BRCMF_CONSOLE_BUFSIZE_OFFSET,
           &firmware->console_buffer_size_)) != ZX_OK) {
    BRCMF_ERR("Failed to read console buffer size: %s", zx_status_get_string(status));
    return status;
  }

  *out_firmware = std::move(firmware);
  return ZX_OK;
}

uint8_t PcieFirmware::GetSharedRamVersion() const { return shared_ram_info_->version; }

uint16_t PcieFirmware::GetSharedRamFlags() const { return shared_ram_info_->flags; }

uint16_t PcieFirmware::GetMaxRxbufpost() const {
  if (shared_ram_info_->max_rxbufpost == 0) {
    return kDefaultMaxRxbufpost;
  }
  return shared_ram_info_->max_rxbufpost;
}

uint32_t PcieFirmware::GetRxDataOffset() const { return shared_ram_info_->rx_data_offset; }

uint32_t PcieFirmware::GetDeviceToHostMailboxDataAddress() const {
  return shared_ram_info_->d2h_mb_data_addr;
}

uint32_t PcieFirmware::GetRingInfoOffset() const { return shared_ram_info_->ring_info_addr; }

std::string PcieFirmware::ReadConsole() {
  zx_status_t status = ZX_OK;

  // Optimization: estimated line length of a typical console log line.  Not required for
  // correctness.
  constexpr size_t kConsoleReserveLength = 64;

  std::string line;
  uint32_t end = 0;
  if ((status = buscore_->TcmRead<uint32_t>(
           shared_ram_info_->console_addr + BRCMF_CONSOLE_WRITEIDX_OFFSET, &end)) != ZX_OK) {
    BRCMF_ERR("Failed to read console write index: %s", zx_status_get_string(status));
    return line;
  }
  while (console_read_index_ != end) {
    uint8_t c = 0;
    if ((status = buscore_->TcmRead<uint8_t>(console_buffer_addr_ + console_read_index_, &c)) !=
        ZX_OK) {
      BRCMF_ERR("Failed to read console character: %s", zx_status_get_string(status));
      return line;
    }
    console_read_index_ += 1;
    if (console_read_index_ >= console_buffer_size_) {
      console_read_index_ = 0;
    }

    if (c == '\n') {
      // When a complete line is found, return the accumulated line and clear the accumulator.
      line = std::move(console_line_);
      console_line_.clear();
      console_line_.reserve(kConsoleReserveLength);
      return line;
    } else if (c == '\r') {
      // Ignore '\r', since the firmware console may use DOS-style newlines.
    } else {
      // Regular character, add it to the line.
      console_line_.push_back(c);
    }
  }

  return line;
}

}  // namespace brcmfmac
}  // namespace wlan
