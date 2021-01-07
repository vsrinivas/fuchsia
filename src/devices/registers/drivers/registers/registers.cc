// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/epitaph.h>

#include <ddk/metadata.h>

#include "src/devices/registers/drivers/registers/registers-bind.h"

namespace registers {

namespace {

const static std::map<::llcpp::fuchsia::hardware::registers::Mask::Tag, uint8_t> kTagToBytes = {
    {::llcpp::fuchsia::hardware::registers::Mask::Tag::kR8, 1},
    {::llcpp::fuchsia::hardware::registers::Mask::Tag::kR16, 2},
    {::llcpp::fuchsia::hardware::registers::Mask::Tag::kR32, 4},
    {::llcpp::fuchsia::hardware::registers::Mask::Tag::kR64, 8},
};

template <typename Ty>
std::optional<Ty> GetMask(const ::llcpp::fuchsia::hardware::registers::Mask& mask) {
  if constexpr (std::is_same_v<Ty, uint8_t>) {
    // Need cast to compile
    return static_cast<Ty>(mask.r8());
  }
  if constexpr (std::is_same_v<Ty, uint16_t>) {
    // Need cast to compile
    return static_cast<Ty>(mask.r16());
  }
  if constexpr (std::is_same_v<Ty, uint32_t>) {
    // Need cast to compile
    return static_cast<Ty>(mask.r32());
  }
  if constexpr (std::is_same_v<Ty, uint64_t>) {
    // Need cast to compile
    return static_cast<Ty>(mask.r64());
  }
  return std::nullopt;
}

}  // namespace

template <typename T>
zx_status_t Register<T>::Init(const RegistersMetadataEntry& config) {
  id_ = config.bind_id();

  for (const auto& m : config.masks()) {
    auto mask = GetMask<T>(m.mask());
    if (!mask.has_value()) {
      return ZX_ERR_INTERNAL;
    }
    masks_.emplace(m.mmio_offset(), std::make_pair(mask.value(), m.count()));
  }

  return ZX_OK;
}

template <typename T>
void Register<T>::RegistersConnect(zx::channel chan) {
  zx_status_t status;
  char name[20];
  snprintf(name, sizeof(name), "register-%lu-thread", id_);
  if (!loop_started_ && (status = loop_.StartThread(name)) != ZX_OK) {
    zxlogf(ERROR, "%s: failed to start registers thread: %d", __func__, status);
    fidl_epitaph_write(chan.get(), status);
    return;
  }

  loop_started_ = true;

  status = fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(chan), this);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to bind channel: %d", __func__, status);
  }

  return;
}

// Returns: true if mask requested is covered by allowed mask.
//          false if mask requested is not covered by allowed mask or mask is not found.
template <typename T>
bool Register<T>::VerifyMask(T mask, const uint64_t offset) {
  auto it = masks_.upper_bound(offset);
  if ((offset % sizeof(T)) || (it == masks_.begin())) {
    return false;
  }
  it--;

  auto base_address = it->first;
  auto reg_mask = it->second.first;
  auto reg_count = it->second.second;
  return (((offset - base_address) / sizeof(T) < reg_count) &&
          // Check that mask requested is covered by allowed mask.
          ((mask | reg_mask) == reg_mask));
}

template <typename T>
zx_status_t Register<T>::ReadRegister(uint64_t offset, T mask, T* out_value) {
  if (!VerifyMask(mask, offset)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&mmio_->locks[offset / sizeof(T)]);
  *out_value = mmio_->mmio.ReadMasked(mask, offset);
  return ZX_OK;
}

template <typename T>
zx_status_t Register<T>::WriteRegister(uint64_t offset, T mask, T value) {
  if (!VerifyMask(mask, offset)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&mmio_->locks[offset / sizeof(T)]);
  mmio_->mmio.ModifyBits(value, mask, offset);
  return ZX_OK;
}

template <typename T>
zx_status_t RegistersDevice<T>::Init(zx_device_t* parent, Metadata metadata) {
  zx_status_t status = ZX_OK;

  ddk::PDev pdev(parent);
  pdev_device_info_t device_info = {};
  if ((status = pdev.GetDeviceInfo(&device_info)) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not get device info", __func__);
    return status;
  }
  if (metadata.mmio().count() != device_info.mmio_count) {
    zxlogf(ERROR, "%s: MMIO metadata size doesn't match MMIO count.", __func__);
    return ZX_ERR_INTERNAL;
  }

  // Get MMIOs
  std::map<uint32_t, std::vector<T>> overlap;
  for (uint32_t i = 0; i < device_info.mmio_count; i++) {
    std::optional<ddk::MmioBuffer> tmp_mmio;
    if ((status = pdev.MapMmio(i, &tmp_mmio)) != ZX_OK) {
      zxlogf(ERROR, "%s: Could not get mmio regions", __func__);
      return status;
    }

    auto register_count = tmp_mmio->get_size() / sizeof(T);
    if (tmp_mmio->get_size() % sizeof(T)) {
      zxlogf(ERROR, "%s: MMIO size does not cover full registers", __func__);
      return ZX_ERR_INTERNAL;
    }

    std::vector<fbl::Mutex> tmp_locks(register_count);
    mmios_.emplace(metadata.mmio()[i].id(), std::make_shared<MmioInfo>(MmioInfo{
                                                .mmio = *std::move(tmp_mmio),
                                                .locks = std::move(tmp_locks),
                                            }));

    overlap.emplace(metadata.mmio()[i].id(), std::vector<T>(register_count, 0));
  }

  // Check for overlapping bits.
  for (const auto& reg : metadata.registers()) {
    if (!reg.has_bind_id() && !reg.has_mmio_id() && !reg.has_masks()) {
      // Doesn't have to have all Register IDs.
      continue;
    }

    if (mmios_.find(reg.mmio_id()) == mmios_.end()) {
      zxlogf(ERROR, "%s: Invalid MMIO ID %u for Register %u.\n", __func__, reg.mmio_id(),
             reg.bind_id());
      return ZX_ERR_INTERNAL;
    }

    for (const auto& m : reg.masks()) {
      if (m.mmio_offset() / sizeof(T) >= mmios_[reg.mmio_id()]->locks.size()) {
        zxlogf(ERROR, "%s: Invalid offset.\n", __func__);
        return ZX_ERR_INTERNAL;
      }

      if (!m.overlap_check_on()) {
        continue;
      }
      auto bits = overlap[reg.mmio_id()][m.mmio_offset() / sizeof(T)];
      auto mask = GetMask<T>(m.mask());
      if (!mask.has_value()) {
        zxlogf(ERROR, "%s: Invalid mask\n", __func__);
        return ZX_ERR_INTERNAL;
      }
      auto mask_value = mask.value();
      if (bits & mask_value) {
        zxlogf(ERROR, "%s: Overlapping bits in MMIO ID %u, Register No. %lu, Bit mask 0x%lx\n",
               __func__, reg.mmio_id(), m.mmio_offset() / sizeof(T),
               static_cast<uint64_t>(bits & mask_value));
        return ZX_ERR_INTERNAL;
      }
      overlap[reg.mmio_id()][m.mmio_offset() / sizeof(T)] |= mask_value;
    }
  }

  // Create Registers
  for (auto& reg : metadata.registers()) {
    if (!reg.has_bind_id() && !reg.has_mmio_id() && !reg.has_masks()) {
      // Doesn't have to have all Register IDs.
      continue;
    }

    fbl::AllocChecker ac;
    std::unique_ptr<Register<T>> tmp_register(
        new (&ac) Register<T>(this->zxdev(), mmios_[reg.mmio_id()]));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    zx_device_prop_t props[] = {
        {BIND_REGISTER_ID, 0, reg.bind_id()},
    };
    char name[20];
    snprintf(name, sizeof(name), "register-%u", reg.bind_id());
    auto status = tmp_register->DdkAdd(
        ddk::DeviceAddArgs(name).set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE).set_props(props));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAdd for %s failed %d", __func__, name, status);
      return status;
    }

    auto dev = tmp_register.release();
    dev->Init(reg);
  }

  return ZX_OK;
}

template <typename T>
zx_status_t RegistersDevice<T>::Create(zx_device_t* parent, Metadata metadata) {
  fbl::AllocChecker ac;
  std::unique_ptr<RegistersDevice<T>> device(new (&ac) RegistersDevice(parent));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: device object alloc failed", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = ZX_OK;
  if ((status = device->DdkAdd("registers-device", DEVICE_ADD_NON_BINDABLE)) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed", __func__);
    return status;
  }

  auto* device_ptr = device.release();

  if ((status = device_ptr->Init(parent, std::move(metadata))) != ZX_OK) {
    device_ptr->DdkAsyncRemove();
    zxlogf(ERROR, "%s: Init failed", __func__);
    return status;
  }

  return ZX_OK;
}

zx_status_t Bind(void* ctx, zx_device_t* parent) {
  // Get metadata
  size_t size;
  auto status = device_get_metadata_size(parent, DEVICE_METADATA_REGISTERS, &size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "device_get_metadata_size failed %d", status);
    return status;
  }

  size_t actual;
  auto bytes = std::make_unique<uint8_t[]>(size);
  status = device_get_metadata(parent, DEVICE_METADATA_REGISTERS, bytes.get(), size, &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "device_get_metadata failed %d", status);
    return status;
  }
  if (actual != size) {
    zxlogf(ERROR, "device_get_metadata size error %d", status);
    return ZX_ERR_INTERNAL;
  }

  // Parse
  fidl::DecodedMessage<Metadata> decoded(bytes.get(), static_cast<uint32_t>(size), nullptr, 0);
  if (!decoded.ok() || (decoded.error() != nullptr)) {
    zxlogf(ERROR, "Unable to parse metadata %s", decoded.error());
    return ZX_ERR_INTERNAL;
  }
  const auto& metadata = decoded.PrimaryObject();

  // Validate
  if (!metadata->has_mmio() || !metadata->has_registers()) {
    zxlogf(ERROR, "Metadata incomplete");
    return ZX_ERR_INTERNAL;
  }
  for (const auto& mmio : metadata->mmio()) {
    if (!mmio.has_id()) {
      zxlogf(ERROR, "Metadata incomplete");
      return ZX_ERR_INTERNAL;
    }
  }
  bool begin = true;
  ::llcpp::fuchsia::hardware::registers::Mask::Tag tag;
  for (const auto& reg : metadata->registers()) {
    if (!reg.has_bind_id() && !reg.has_mmio_id() && !reg.has_masks()) {
      // Doesn't have to have all Register IDs.
      continue;
    }

    if (!reg.has_bind_id() || !reg.has_mmio_id() || !reg.has_masks()) {
      zxlogf(ERROR, "Metadata incomplete");
      return ZX_ERR_INTERNAL;
    }

    if (begin) {
      tag = reg.masks().begin()->mask().which();
      begin = false;
    }

    for (const auto& mask : reg.masks()) {
      if (!mask.has_mask() || !mask.has_mmio_offset() || !mask.has_count()) {
        zxlogf(ERROR, "Metadata incomplete");
        return ZX_ERR_INTERNAL;
      }

      if (mask.mask().which() != tag) {
        zxlogf(ERROR, "Width of registers don't match up.");
        return ZX_ERR_INTERNAL;
      }

      if (mask.mmio_offset() % kTagToBytes.at(tag)) {
        zxlogf(ERROR, "%s: Mask with offset 0x%08lx is not aligned", __func__, mask.mmio_offset());
        return ZX_ERR_INTERNAL;
      }
    }
  }

  // Create devices
  switch (tag) {
    case ::llcpp::fuchsia::hardware::registers::Mask::Tag::kR8:
      status = RegistersDevice<uint8_t>::Create(parent, std::move(*metadata));
      break;
    case ::llcpp::fuchsia::hardware::registers::Mask::Tag::kR16:
      status = RegistersDevice<uint16_t>::Create(parent, std::move(*metadata));
      break;
    case ::llcpp::fuchsia::hardware::registers::Mask::Tag::kR32:
      status = RegistersDevice<uint32_t>::Create(parent, std::move(*metadata));
      break;
    case ::llcpp::fuchsia::hardware::registers::Mask::Tag::kR64:
      status = RegistersDevice<uint64_t>::Create(parent, std::move(*metadata));
      break;
  }

  return status;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Bind;
  return ops;
}();

}  // namespace registers

// clang-format off
ZIRCON_DRIVER(registers, registers::driver_ops, "zircon", "0.1");
// clang-format on
