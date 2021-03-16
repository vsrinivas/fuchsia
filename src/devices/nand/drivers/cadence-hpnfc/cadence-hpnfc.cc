// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cadence-hpnfc.h"

#include <endian.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/time.h>
#include <zircon/threads.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "cadence-hpnfc-reg.h"
#include "src/devices/nand/drivers/cadence-hpnfc/cadence-hpnfc-bind.h"

namespace {

struct JedecIdMap {
  uint8_t jedec_id[2];
  const char* manufacturer;
  const char* device;
  uint32_t page_size;
  uint32_t pages_per_block;
  uint32_t num_blocks;
  uint32_t ecc_bits;
  uint32_t oob_size;
};

constexpr JedecIdMap kJedecIdMap[] = {
    {
        .jedec_id = {0x98, 0xdc},
        .manufacturer = "Toshiba",
        .device = "TC58NVG2S0Hxxxx",
        .page_size = 4096,
        .pages_per_block = 64,
        .num_blocks = 2048,
        .ecc_bits = 8,
        .oob_size = 256,
    },
};

// Row address bits 5 and below are the page address, 6 and above are the block address.
constexpr uint32_t kBlockAddressIndex = 6;
constexpr uint32_t kPagesPerBlock = 1 << kBlockAddressIndex;
// Selects BCH correction strength 48 from BCH config registers.
constexpr uint32_t kEccCorrectionStrength = 5;

constexpr uint32_t kMaxOobSize = 32;

constexpr uint32_t kParameterPageSize = 256;
static_assert(kParameterPageSize % sizeof(uint32_t) == 0);
constexpr uint8_t kParameterPageSignature[] = {0x4f, 0x4e, 0x46, 0x49};

// Only the first two bytes are needed but the controller requires that we round up to eight bytes.
constexpr uint32_t kJedecIdSize = 8;

// These values were taken from the bootloader NAND driver.
constexpr zx::duration kWaitDelay = zx::usec(50);
constexpr uint32_t kTimeoutCount = 8000;

constexpr uint32_t kBytesToMebibytes = 1024 * 1024;

inline uint16_t ReadParameterPage16(const uint8_t* buffer, uint32_t offset) {
  const uint16_t* buffer16 = reinterpret_cast<const uint16_t*>(buffer);
  ZX_DEBUG_ASSERT(offset % sizeof(uint16_t) == 0);
  return letoh16(buffer16[offset / sizeof(uint16_t)]);
}

inline uint32_t ReadParameterPage32(const uint8_t* buffer, uint32_t offset) {
  const uint32_t* buffer32 = reinterpret_cast<const uint32_t*>(buffer);
  ZX_DEBUG_ASSERT(offset % sizeof(uint32_t) == 0);
  return letoh32(buffer32[offset / sizeof(uint32_t)]);
}

}  // namespace

namespace rawnand {

// TODO(bradenkell): Use DMA.

zx_status_t CadenceHpnfc::Create(void* ctx, zx_device_t* parent) {
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PLATFORM_DEVICE", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> mmio;
  zx_status_t status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map MMIO: %d", __FILE__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> fifo_mmio;
  if ((status = pdev.MapMmio(1, &fifo_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map FIFO MMIO: %d", __FILE__, status);
    return status;
  }

  zx::interrupt interrupt;
  if ((status = pdev.GetInterrupt(0, &interrupt)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get interrupt: %d", __FILE__, status);
    return status;
  }

  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<CadenceHpnfc>(&ac, parent, *std::move(mmio),
                                                       *std::move(fifo_mmio), std::move(interrupt));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Failed to allocate device memory", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->StartInterruptThread()) != ZX_OK) {
    return status;
  }

  if ((status = device->Init()) != ZX_OK) {
    device->StopInterruptThread();
    return status;
  }

  if ((status = device->Bind()) != ZX_OK) {
    device->StopInterruptThread();
    return status;
  }

  __UNUSED auto* dummy = device.release();
  return ZX_OK;
}

zx_status_t CadenceHpnfc::Bind() {
  zx_status_t status = DdkAdd("cadence-hpnfc");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __FILE__, status);
  }
  return status;
}

bool CadenceHpnfc::WaitForRBn() {
  auto rbn = RbnSettings::Get().ReadFrom(&mmio_);
  for (uint32_t i = 0; !rbn.rbn() && i < kTimeoutCount; i++) {
    zx::nanosleep(zx::deadline_after(kWaitDelay));
    rbn.ReadFrom(&mmio_);
  }
  return rbn.rbn();
}

bool CadenceHpnfc::WaitForThread() {
  auto reg = TrdStatus::Get().ReadFrom(&mmio_);
  for (uint32_t i = 0; reg.thread_busy(0) && i < kTimeoutCount; i++) {
    zx::nanosleep(zx::deadline_after(kWaitDelay));
    reg.ReadFrom(&mmio_);
  }
  return !reg.thread_busy(0);
}

zx_status_t CadenceHpnfc::WaitForSdmaTrigger() {
  if (sync_completion_wait(&completion_, zx::sec(10).get()) != ZX_OK) {
    zxlogf(ERROR, "%s: Timed out waiting for FIFO data", __FILE__);
    return ZX_ERR_TIMED_OUT;
  }

  fbl::AutoLock lock(&lock_);

  sync_completion_reset(&completion_);
  zx_status_t status = sdma_status_;
  sdma_status_ = ZX_ERR_BAD_STATE;

  return status;
}

bool CadenceHpnfc::WaitForCommandComplete() {
  if (sync_completion_wait(&completion_, zx::sec(10).get()) != ZX_OK) {
    zxlogf(ERROR, "%s: Timed out waiting for command to complete", __FILE__);
    return false;
  }

  fbl::AutoLock lock(&lock_);

  sync_completion_reset(&completion_);
  bool complete = cmd_complete_;
  cmd_complete_ = false;

  return complete;
}

zx_status_t CadenceHpnfc::StartInterruptThread() {
  fbl::AutoLock lock(&lock_);
  int thread_status = thrd_create_with_name(
      &interrupt_thread_,
      [](void* ctx) -> int { return reinterpret_cast<CadenceHpnfc*>(ctx)->InterruptThread(); },
      this, "cadence-hpnfc-thread");
  if (thread_status != thrd_success) {
    zxlogf(ERROR, "%s: Failed to create interrupt thread", __FILE__);
    return thrd_status_to_zx_status(thread_status);
  }

  thread_started_ = true;
  return ZX_OK;
}

void CadenceHpnfc::StopInterruptThread() {
  bool should_join = false;
  {
    fbl::AutoLock lock(&lock_);
    should_join = thread_started_;
  }

  interrupt_.destroy();

  if (should_join) {
    thrd_join(interrupt_thread_, nullptr);
  }
}

zx_status_t CadenceHpnfc::Init() {
  CmdStatusPtr::Get().ReadFrom(&mmio_).set_thread_status_select(0).WriteTo(&mmio_);

  IntrStatus::Get().ReadFrom(&mmio_).clear().WriteTo(&mmio_);
  IntrEnable::Get()
      .FromValue(0)
      .set_interrupts_enable(1)
      .set_sdma_error_enable(1)
      .set_sdma_trigger_enable(1)
      .set_cmd_ignored_enable(1)
      .WriteTo(&mmio_);

  if (!WaitForThread())
    return ZX_ERR_TIMED_OUT;

  CmdReg1::Get().FromValue(0).WriteTo(&mmio_);
  CmdReg0::Get()
      .FromValue(0)
      .set_command_type(CmdReg0::kCommandTypePio)
      .set_thread_number(0)
      .set_volume_id(0)
      .set_command_code(CmdReg0::kCommandCodeReset)
      .WriteTo(&mmio_);

  if (!WaitForRBn())
    return ZX_ERR_TIMED_OUT;

  if (PopulateNandInfoOnfi() != ZX_OK && PopulateNandInfoJedec() != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get NAND device info", __FILE__);
    return ZX_ERR_NOT_FOUND;
  }

  // TODO(bradenkell): Check the NAND info we got against the corresponding values in the
  //                   partition map metadata.
  // TODO(bradenkell): Calculate the following values instead of hard coding them.

  NfDevLayout::Get()
      .FromValue(0)
      .set_block_addr_idx(kBlockAddressIndex)
      .set_lun_count(1)
      .set_pages_per_block(kPagesPerBlock)
      .WriteTo(&mmio_);

  const uint32_t sector_size = nand_info_.page_size / 2;
  TransferCfg0::Get().FromValue(0).set_sector_count(2).WriteTo(&mmio_);
  TransferCfg1::Get()
      .FromValue(0)
      .set_last_sector_size(sector_size + nand_info_.oob_size)
      .set_sector_size(sector_size)
      .WriteTo(&mmio_);

  EccConfig0::Get()
      .FromValue(0)
      .set_correction_strength(kEccCorrectionStrength)
      .set_scrambler_enable(0)
      .set_erase_detection_enable(1)
      .set_ecc_enable(1)
      .WriteTo(&mmio_);
  EccConfig1::Get().FromValue(0).WriteTo(&mmio_);

  return ZX_OK;
}

size_t CadenceHpnfc::CopyFromFifo(void* buffer, size_t size) {
  const size_t word_count = size / sizeof(uint32_t);

  if (buffer == nullptr) {
    for (uint32_t i = 0; i < word_count; i++) {
      fifo_mmio_.Read32(0);
    }

    return 0;
  }

  uint32_t* const word_buffer = reinterpret_cast<uint32_t*>(buffer);
  for (uint32_t i = 0; i < word_count; i++) {
    word_buffer[i] = fifo_mmio_.Read32(0);
  }

  return word_count;
}

void CadenceHpnfc::CopyToFifo(const void* buffer, size_t size) {
  const size_t word_count = size / sizeof(uint32_t);

  if (buffer == nullptr) {
    for (uint32_t i = 0; i < word_count; i++) {
      fifo_mmio_.Write32(0xffff'ffff, 0);
    }
  } else {
    const uint32_t* const word_buffer = reinterpret_cast<const uint32_t*>(buffer);
    for (uint32_t i = 0; i < word_count; i++) {
      fifo_mmio_.Write32(word_buffer[i], 0);
    }
  }
}

zx_status_t CadenceHpnfc::PopulateNandInfoJedec() {
  uint8_t jedec_id[kJedecIdSize] = {};
  zx_status_t status = DoGenericCommand(kInstructionTypeReadId, jedec_id, sizeof(jedec_id));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read ID: %d", __FILE__, status);
    return status;
  }

  for (size_t i = 0; i < std::size(kJedecIdMap); i++) {
    if (kJedecIdMap[i].jedec_id[0] == jedec_id[0] && kJedecIdMap[i].jedec_id[1] == jedec_id[1]) {
      nand_info_.page_size = kJedecIdMap[i].page_size;
      nand_info_.pages_per_block = kJedecIdMap[i].pages_per_block;
      nand_info_.num_blocks = kJedecIdMap[i].num_blocks;
      nand_info_.ecc_bits = kJedecIdMap[i].ecc_bits;
      nand_info_.oob_size = std::min(kJedecIdMap[i].oob_size, kMaxOobSize);
      nand_info_.nand_class = NAND_CLASS_PARTMAP;
      memset(nand_info_.partition_guid, 0, sizeof(nand_info_.partition_guid));

      const uint64_t capacity = static_cast<uint64_t>(nand_info_.page_size) *
                                nand_info_.pages_per_block * nand_info_.num_blocks;

      zxlogf(INFO, "CadenceHpnfc: Found NAND device %s with capacity %ld MiB",
             kJedecIdMap[i].device, capacity / kBytesToMebibytes);

      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t CadenceHpnfc::PopulateNandInfoOnfi() {
  uint8_t parameter_page[kParameterPageSize] = {};
  zx_status_t status =
      DoGenericCommand(kInstructionTypeReadParameterPage, parameter_page, sizeof(parameter_page));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to read parameter page: %d", __FILE__, status);
    return status;
  }

  if (memcmp(parameter_page, kParameterPageSignature, sizeof(kParameterPageSignature)) != 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  constexpr uint32_t kDeviceModelOffset = 44;
  constexpr uint32_t kDeviceModelSize = 20;
  constexpr uint32_t kPageSizeOffset = 80;
  constexpr uint32_t kOobSizeOffset = 84;
  constexpr uint32_t kPagesPerBlockOffset = 92;
  constexpr uint32_t kBlocksPerLunOffset = 96;
  constexpr uint32_t kLunsOffset = 100;
  constexpr uint32_t kEccBitsCorrectabilityOffset = 112;

  // TODO(bradenkell): Read the Extended ECC Information if this is 0xff.
  ZX_DEBUG_ASSERT(parameter_page[kEccBitsCorrectabilityOffset] != 0xff);

  nand_info_.page_size = ReadParameterPage32(parameter_page, kPageSizeOffset);
  nand_info_.pages_per_block = ReadParameterPage32(parameter_page, kPagesPerBlockOffset);
  nand_info_.num_blocks =
      ReadParameterPage32(parameter_page, kBlocksPerLunOffset) * parameter_page[kLunsOffset];
  nand_info_.ecc_bits = parameter_page[kEccBitsCorrectabilityOffset];
  nand_info_.oob_size =
      std::min<uint32_t>(ReadParameterPage16(parameter_page, kOobSizeOffset), kMaxOobSize);
  nand_info_.nand_class = NAND_CLASS_PARTMAP;
  memset(nand_info_.partition_guid, 0, sizeof(nand_info_.partition_guid));

  ZX_DEBUG_ASSERT(nand_info_.page_size % sizeof(uint32_t) == 0);
  ZX_DEBUG_ASSERT(nand_info_.oob_size % sizeof(uint32_t) == 0);

  const uint64_t capacity = static_cast<uint64_t>(nand_info_.page_size) *
                            nand_info_.pages_per_block * nand_info_.num_blocks;

  char model[kDeviceModelSize + 1];
  memcpy(model, parameter_page + kDeviceModelOffset, kDeviceModelSize);

  char* const first_space = reinterpret_cast<char*>(memchr(model, ' ', kDeviceModelSize));
  model[first_space ? (first_space - model) : kDeviceModelSize] = '\0';

  zxlogf(INFO, "CadenceHpnfc: Found NAND device %s with capacity %ld MiB", model,
         capacity / kBytesToMebibytes);

  return ZX_OK;
}

zx_status_t CadenceHpnfc::DoGenericCommand(uint32_t instruction, uint8_t* out_data, uint32_t size) {
  if (!WaitForThread())
    return ZX_ERR_TIMED_OUT;

  IntrStatus::Get().ReadFrom(&mmio_).clear().WriteTo(&mmio_);

  CmdReg2Command::Get().FromValue(0).set_instruction_type(instruction).WriteTo(&mmio_);
  CmdReg3::Get().FromValue(0).WriteTo(&mmio_);
  CmdReg0::Get().FromValue(0).set_command_type(CmdReg0::kCommandTypeGeneric).WriteTo(&mmio_);

  if (!WaitForRBn())
    return ZX_ERR_TIMED_OUT;

  CmdReg1::Get().FromValue(0).WriteTo(&mmio_);
  CmdReg2Data::Get().FromValue(0).set_instruction_type(kInstructionTypeData).WriteTo(&mmio_);
  CmdReg3::Get().FromValue(0).set_last_sector_size(size).set_sector_count(1).WriteTo(&mmio_);
  CmdReg0::Get().FromValue(0).set_command_type(CmdReg0::kCommandTypeGeneric).WriteTo(&mmio_);

  zx_status_t status = WaitForSdmaTrigger();
  if (status != ZX_OK)
    return status;

  CopyFromFifo(out_data, size);

  return ZX_OK;
}

zx_status_t CadenceHpnfc::RawNandReadPageHwecc(uint32_t nandpage, uint8_t* out_data_buffer,
                                               size_t data_size, size_t* out_data_actual,
                                               uint8_t* out_oob_buffer, size_t oob_size,
                                               size_t* out_oob_actual, uint32_t* out_ecc_correct) {
  if (data_size < nand_info_.page_size || oob_size < nand_info_.oob_size) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (!WaitForThread())
    return ZX_ERR_TIMED_OUT;

  IntrStatus::Get().ReadFrom(&mmio_).clear().WriteTo(&mmio_);

  CmdReg1::Get().FromValue(0).set_address(nandpage).WriteTo(&mmio_);
  CmdReg2Dma::Get().FromValue(0).WriteTo(&mmio_);
  CmdReg3::Get().FromValue(0).WriteTo(&mmio_);
  CmdReg0::Get()
      .FromValue(0)
      .set_command_type(CmdReg0::kCommandTypePio)
      .set_dma_sel(0)
      .set_command_code(CmdReg0::kCommandCodeReadPage)
      .WriteTo(&mmio_);

  zx_status_t status = WaitForSdmaTrigger();
  if (status != ZX_OK)
    return status;

  const uint32_t sdma_size = SdmaSize::Get().ReadFrom(&mmio_).reg_value();
  if (sdma_size != nand_info_.page_size + nand_info_.oob_size) {
    zxlogf(ERROR, "%s: Expected %u bytes in FIFO, got %u", __FILE__,
           nand_info_.page_size + nand_info_.oob_size, sdma_size);
    return ZX_ERR_IO;
  }

  data_size = CopyFromFifo(out_data_buffer, nand_info_.page_size);
  oob_size = CopyFromFifo(out_oob_buffer, nand_info_.oob_size);

  auto cmd_status = CmdStatus::Get().ReadFrom(&mmio_);

  if (out_data_actual != nullptr) {
    *out_data_actual = data_size;
  }
  if (out_oob_actual != nullptr) {
    *out_oob_actual = oob_size;
  }
  if (out_ecc_correct != nullptr) {
    *out_ecc_correct = cmd_status.max_errors();
  }

  if (cmd_status.ecc_error()) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  } else if (cmd_status.bus_error() || cmd_status.fail() || cmd_status.dev_error() ||
             cmd_status.cmd_error()) {
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t CadenceHpnfc::RawNandWritePageHwecc(const uint8_t* data_buffer, size_t data_size,
                                                const uint8_t* oob_buffer, size_t oob_size,
                                                uint32_t nandpage) {
  if (data_size < nand_info_.page_size || oob_size < nand_info_.oob_size) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (!WaitForThread())
    return ZX_ERR_TIMED_OUT;

  IntrStatus::Get().ReadFrom(&mmio_).clear().WriteTo(&mmio_);

  CmdReg1::Get().FromValue(0).set_address(nandpage).WriteTo(&mmio_);
  CmdReg2Dma::Get().FromValue(0).WriteTo(&mmio_);
  CmdReg3::Get().FromValue(0).WriteTo(&mmio_);
  CmdReg0::Get()
      .FromValue(0)
      .set_command_type(CmdReg0::kCommandTypePio)
      .set_dma_sel(0)
      .set_command_code(CmdReg0::kCommandCodeProgramPage)
      .WriteTo(&mmio_);

  zx_status_t status = WaitForSdmaTrigger();
  if (status != ZX_OK)
    return status;

  const uint32_t sdma_size = SdmaSize::Get().ReadFrom(&mmio_).reg_value();
  if (SdmaSize::Get().ReadFrom(&mmio_).reg_value() != nand_info_.page_size + nand_info_.oob_size) {
    zxlogf(ERROR, "%s: Expected %u bytes in FIFO, got %u", __FILE__,
           nand_info_.page_size + nand_info_.oob_size, sdma_size);
    return ZX_ERR_IO;
  }

  CopyToFifo(data_buffer, nand_info_.page_size);
  CopyToFifo(oob_buffer, nand_info_.oob_size);

  auto cmd_status = CmdStatus::Get().ReadFrom(&mmio_);
  if (cmd_status.bus_error() || cmd_status.fail() || cmd_status.dev_error() ||
      cmd_status.ecc_error() || cmd_status.cmd_error()) {
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t CadenceHpnfc::RawNandEraseBlock(uint32_t nandpage) {
  if (!WaitForThread())
    return ZX_ERR_TIMED_OUT;

  IntrStatus::Get().ReadFrom(&mmio_).clear().WriteTo(&mmio_);

  CmdReg1::Get().FromValue(0).set_address(nandpage).WriteTo(&mmio_);
  CmdReg2Dma::Get().FromValue(0).WriteTo(&mmio_);
  CmdReg3::Get().FromValue(0).WriteTo(&mmio_);
  CmdReg0::Get()
      .FromValue(0)
      .set_command_type(CmdReg0::kCommandTypePio)
      .set_interrupt_enable(1)
      .set_command_code(CmdReg0::kCommandCodeEraseBlock)
      .WriteTo(&mmio_);

  if (!WaitForCommandComplete())
    return ZX_ERR_TIMED_OUT;

  auto cmd_status = CmdStatus::Get().ReadFrom(&mmio_);
  if (cmd_status.bus_error() || cmd_status.fail() || cmd_status.dev_error() ||
      cmd_status.max_errors() || cmd_status.ecc_error() || cmd_status.cmd_error()) {
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

int CadenceHpnfc::InterruptThread() {
  for (;;) {
    zx_status_t status = interrupt_.wait(nullptr);
    if (status == ZX_ERR_CANCELED) {
      break;
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Interrupt wait failed: %d", __FILE__, status);
      return thrd_error;
    }

    auto intr_status = IntrStatus::Get().ReadFrom(&mmio_).WriteTo(&mmio_);
    auto thrd_status = TrdCompIntrStatus::Get().ReadFrom(&mmio_).WriteTo(&mmio_);

    fbl::AutoLock lock(&lock_);

    if (intr_status.sdma_trigger()) {
      sdma_status_ = ZX_OK;
      sync_completion_signal(&completion_);
    } else if (intr_status.cmd_ignored()) {
      sdma_status_ = ZX_ERR_NOT_SUPPORTED;
      sync_completion_signal(&completion_);
    } else if (intr_status.sdma_error()) {
      sdma_status_ = ZX_ERR_IO;
      sync_completion_signal(&completion_);
    } else if (thrd_status.thread_complete(0)) {
      cmd_complete_ = true;
      sync_completion_signal(&completion_);
    }
  }

  return thrd_success;
}

zx_status_t CadenceHpnfc::RawNandGetNandInfo(nand_info_t* out_info) {
  memcpy(out_info, &nand_info_, sizeof(nand_info_));
  return ZX_OK;
}

void CadenceHpnfc::DdkUnbind(ddk::UnbindTxn txn) {
  StopInterruptThread();
  txn.Reply();
}

void CadenceHpnfc::DdkRelease() { delete this; }

}  // namespace rawnand

static zx_driver_ops_t cadence_hpnfc_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = rawnand::CadenceHpnfc::Create;
  return ops;
}();

ZIRCON_DRIVER(cadence_hpnfc, cadence_hpnfc_driver_ops, "zircon", "0.1");
