// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qcom-pil.h"

#include <elf.h>
#include <lib/mmio/mmio.h>

#include <algorithm>
#include <limits>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddktl/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/string_buffer.h>
#include <qcom/smc.h>

namespace qcom_pil {

zx_status_t PilDevice::LoadAuthFirmware(size_t fw_n) {
  // TODO(andresoportus): Until we load from nonvolatile memory return "not supported".  Set
  // fw_included to true if FW is included in the build for testing, see BUILD.gn for file list.
  constexpr bool fw_included = false;
  if (!fw_included) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Load the metadata.
  fbl::StringBuffer<metadata::kMaxNameLen + 4> metadata_file;
  metadata_file.Append(fw_[fw_n].name);
  metadata_file.Append(".mdt");
  zxlogf(INFO, "%s loading %s\n", __func__, metadata_file.c_str());
  size_t metadata_size = 0;
  zx::vmo metadata;
  auto status = load_firmware(parent(), metadata_file.c_str(), metadata.reset_and_get_address(),
                              &metadata_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s load FW metadata failed %d\n", __func__, status);
    return status;
  }

  // Get ELF segment info used for arrangement in memory.
  Elf32_Ehdr ehdr;
  metadata.read(&ehdr, 0, sizeof(Elf32_Ehdr));
  if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG)) {
    zxlogf(ERROR, "%s not an ELF header\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }
  fbl::Array<Elf32_Phdr> phdrs(new Elf32_Phdr[ehdr.e_phnum], ehdr.e_phnum);
  metadata.read(phdrs.data(), ehdr.e_phoff, ehdr.e_phnum * sizeof(Elf32_Phdr));

  // Copy metadata to the intended physical address.
  status = zx_vmo_read(metadata.get(), mmios_[fw_n]->get(), 0, ROUNDUP(metadata_size, PAGE_SIZE));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s VMO read failed %d\n", __func__, status);
    return status;
  }

  // Initialize the metadata in physical memory via SMC call.
  zx_smc_parameters_t params = {};
  zx_smc_result_t result = {};
  params = CreatePilSmcParams(PilCmd::InitImage,
                              CreateSmcArgs(2, SmcArgType::kValue, SmcArgType::kBufferReadWrite),
                              fw_[fw_n].id,   // kValue.
                              fw_[fw_n].pa);  // kBufferReadWrite.
  status = qcom::SmcCall(smc_.get(), &params, &result);
  if (status != ZX_OK || result.arg0 != qcom::kSmcOk) {
    zxlogf(ERROR, "%s metadata init failed %d/%d\n", __func__, status,
           static_cast<int>(result.arg0));
    return status;
  }

  // Calculate total size in physical memory.
  size_t total_size = 0;
  uint32_t start = std::numeric_limits<uint32_t>::max();
  uint32_t end = 0;
  for (int i = 0; i < ehdr.e_phnum; ++i) {
    if (phdrs[i].p_type != PT_LOAD) {
      continue;
    }
    constexpr uint32_t kRelocatableBitOffset = 27;
    if (!(phdrs[i].p_flags & (1 << kRelocatableBitOffset))) {
      zxlogf(ERROR, "%s FW segments to load must be relocatable\n", __func__);
      return ZX_ERR_INTERNAL;
    }
    if (phdrs[i].p_paddr < start) {
      start = phdrs[i].p_paddr;
    }
    if (phdrs[i].p_paddr + phdrs[i].p_memsz > end) {
      end = phdrs[i].p_paddr + phdrs[i].p_memsz;
    }
  }
  if (start == std::numeric_limits<uint32_t>::max() || end == 0) {
    zxlogf(ERROR, "%s ELF headers could not find total size\n", __func__);
    return ZX_ERR_INTERNAL;
  }
  total_size = ROUNDUP(end - start, PAGE_SIZE);
  if (total_size > mmios_[fw_n]->get_size()) {
    zxlogf(ERROR, "%s ELF headers total size (0x%lX) too big (>0x%lX)\n", __func__, total_size,
           mmios_[fw_n]->get_size());
    return ZX_ERR_INTERNAL;
  }

  // Setup physical memory before authentication via SMC call.
  params = CreatePilSmcParams(
      PilCmd::MemSetup,
      CreateSmcArgs(3, SmcArgType::kValue, SmcArgType::kValue, SmcArgType::kValue),
      fw_[fw_n].id,  // kValue.
      fw_[fw_n].pa,  // kValue, not clear why not a kBuffer...
      total_size);   // kValue.
  status = qcom::SmcCall(smc_.get(), &params, &result);
  if (status != ZX_OK || result.arg0 != qcom::kSmcOk) {
    zxlogf(ERROR, "%s memory setup failed %d/%d\n", __func__, status,
           static_cast<int>(result.arg0));
    return status;
  }

  // Get virtual address range for the intended physical address.
  zx_vaddr_t v_addr = reinterpret_cast<zx_vaddr_t>(mmios_[fw_n]->get());

  // Load all segments.
  for (int i = 0; i < ehdr.e_phnum; ++i) {
    if (phdrs[i].p_type != PT_LOAD || phdrs[i].p_filesz == 0) {
      continue;
    }

    fbl::StringBuffer<metadata::kMaxNameLen + 4> segment_name;
    segment_name.Append(fw_[fw_n].name);
    segment_name.AppendPrintf(".b%02u", i);

    zxlogf(INFO, "%s loading %s\n", __func__, segment_name.c_str());
    zx::vmo segment;
    size_t seg_size = 0;
    auto status =
        load_firmware(parent(), segment_name.c_str(), segment.reset_and_get_address(), &seg_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s load FW failed %d\n", __func__, status);
      return status;
    }

    status = segment.read(reinterpret_cast<void*>(v_addr + (phdrs[i].p_paddr - start)), 0,
                          ROUNDUP(seg_size, PAGE_SIZE));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s vmo read failed %d\n", __func__, status);
      return status;
    }
  }

  // Authenticates the whole image via SMC call.
  params =
      CreatePilSmcParams(PilCmd::AuthAndReset, CreateSmcArgs(1, SmcArgType::kValue), fw_[fw_n].id);
  status = qcom::SmcCall(smc_.get(), &params, &result);
  if (status != ZX_OK || result.arg0 != qcom::kSmcOk) {
    zxlogf(ERROR, "%s authentication failed %d/%d\n", __func__, status,
           static_cast<int>(result.arg0));
    return status;
  }
  zxlogf(INFO, "%s %s brought out of reset\n", __func__, fw_[fw_n].name);
  return ZX_OK;
}

int PilDevice::PilThread() {
  for (size_t i = 0; i < fw_.size(); ++i) {
    LoadAuthFirmware(i);
  }
  return 0;
}

zx_status_t PilDevice::Bind() {
  ddk::CompositeProtocolClient composite(parent());
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s could not get composite protocol\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_device_t* fragments[kClockCount + 1];
  size_t actual;
  composite.GetFragments(fragments, fbl::count_of(fragments), &actual);
  if (actual != fbl::count_of(fragments)) {
    zxlogf(ERROR, "%s could not get fragments\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_ = fragments[0];
  if (!pdev_.is_valid()) {
    zxlogf(ERROR, "%s could not get pdev protocol\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto status = pdev_.GetSmc(0, &smc_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s GetSmc failed %d\n", __func__, status);
    return status;
  }
  status = pdev_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s GetBti failed %d\n", __func__, status);
    return status;
  }

  for (unsigned i = 0; i < kClockCount; i++) {
    clks_[i] = fragments[i + 1];
    if (!clks_[i].is_valid()) {
      zxlogf(ERROR, "%s GetClk failed %d\n", __func__, status);
      return status;
    }
  }

  size_t metadata_size = 0;
  status = device_get_metadata_size(parent_, DEVICE_METADATA_PRIVATE, &metadata_size);
  if (status != ZX_OK) {
    return ZX_OK;
  }
  size_t n_fw_images = metadata_size / sizeof(metadata::Firmware);
  fbl::AllocChecker ac;
  fw_ = fbl::Array(new (&ac) metadata::Firmware[n_fw_images], n_fw_images);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  status =
      device_get_metadata(parent_, DEVICE_METADATA_PRIVATE, fw_.data(), metadata_size, &actual);
  if (status != ZX_OK || metadata_size != actual) {
    zxlogf(ERROR, "%s device_get_metadata failed %d\n", __func__, status);
    return status;
  }

  mmios_ = fbl::Array(new (&ac) std::optional<ddk::MmioBuffer>[n_fw_images], n_fw_images);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < n_fw_images; ++i) {
    status = pdev_.MapMmio(i, &mmios_[i]);
    if (status != ZX_OK) {
      return status;
    }
  }

// Used to test communication with QSEE and its replies for different image ids.
//#define TEST_SMC
#ifdef TEST_SMC
  for (int i = 0; i < 16; ++i) {
    auto params = CreatePilSmcParams(PilCmd::QuerySupport, CreateSmcArgs(1, SmcArgType::kValue), i);
    zx_smc_result_t result = {};
    status = qcom::SmcCall(smc_.get(), &params, &result);
    if (status == ZX_OK && result.arg0 == kSmcOk && result.arg1 == 1) {
      zxlogf(INFO, "%s pas_id %d supported\n", __func__, i);
    }
  }
#endif

  clks_[kCryptoAhbClk].Enable();
  clks_[kCryptoAxiClk].Enable();
  clks_[kCryptoClk].Enable();

  auto thunk = [](void* arg) -> int { return reinterpret_cast<PilDevice*>(arg)->PilThread(); };
  int rc = thrd_create_with_name(&pil_thread_, thunk, this, "qcom-pil");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  status = DdkAdd("qcom-pil");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAdd failed %d\n", __func__, status);
    ShutDown();
    return status;
  }
  return ZX_OK;
}
zx_status_t PilDevice::Init() { return ZX_OK; }

void PilDevice::ShutDown() {}

void PilDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void PilDevice::DdkRelease() { delete this; }

zx_status_t PilDevice::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<PilDevice>(&ac, parent);
  if (!ac.check()) {
    zxlogf(ERROR, "%s PilDevice creation ZX_ERR_NO_MEMORY\n", __func__);
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the memory for dev
  auto ptr = dev.release();
  return ptr->Init();
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = PilDevice::Create;
  return ops;
}();

}  // namespace qcom_pil

// clang-format off
ZIRCON_DRIVER_BEGIN(qcom_pil, qcom_pil::driver_ops, "zircon", "0.1", 3)
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_QUALCOMM),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_QUALCOMM_PIL),
ZIRCON_DRIVER_END(qcom_pil)
