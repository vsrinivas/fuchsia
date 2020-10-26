// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <lib/device-protocol/pdev.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/epitaph.h>

#include <ddk/metadata.h>
#include <ddk/protocol/platform/bus.h>

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
  id_ = config.id();
  base_address_ = config.base_address();

  uint32_t reg_count = 0;
  for (const auto& m : config.masks()) {
    reg_count += m.count();
    auto mask = GetMask<T>(m.mask());
    if (!mask.has_value()) {
      return ZX_ERR_INTERNAL;
    }
    masks_.emplace(reg_count, mask.value());
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
bool Register<T>::VerifyMask(T mask, const uint64_t register_offset) {
  auto it = masks_.upper_bound(register_offset);
  if (it == masks_.end()) {
    return false;
  }
  // Check that mask requested is covered by allowed mask.
  auto reg_mask = it->second;
  return ((mask | reg_mask) == reg_mask);
}

template <typename T>
zx_status_t Register<T>::ReadRegister(uint64_t address, T mask, T* out_value) {
  if ((address % sizeof(T)) ||  // Aligned to register
      masks_.empty() ||         // Non-empty masks
                                // Address within register range
      (address < base_address_) ||
      ((address - base_address_) / sizeof(T) >= masks_.rbegin()->first) ||
      // Mask within valid range
      !VerifyMask(mask, (address - base_address_) / sizeof(T))) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Valid because when binding, register is aligned and wholly within mmio
  auto mmio_offset = address - mmio_->base_address;
  fbl::AutoLock lock(&mmio_->locks[mmio_offset / sizeof(T)]);
  *out_value = mmio_->mmio.ReadMasked(mask, mmio_offset);
  return ZX_OK;
}

template <typename T>
zx_status_t Register<T>::WriteRegister(uint64_t address, T mask, T value) {
  if ((address % sizeof(T)) ||  // Aligned to register
      masks_.empty() ||         // Non-empty masks
                                // Address within register range
      (address < base_address_) ||
      ((address - base_address_) / sizeof(T) >= masks_.rbegin()->first) ||
      // Mask within valid range
      !VerifyMask(mask, (address - base_address_) / sizeof(T))) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Valid because when binding, register is aligned and wholly within mmio
  auto mmio_offset = address - mmio_->base_address;
  fbl::AutoLock lock(&mmio_->locks[mmio_offset / sizeof(T)]);
  mmio_->mmio.ModifyBits(value, mask, mmio_offset);
  return ZX_OK;
}

// FindMmio: return mmio index if register fits in some MMIO. return invalid mmio index (= mmio
// size) if register doesn't fit in MMIO.
template <typename T>
std::optional<uint32_t> RegistersDevice<T>::FindMmio(const RegistersMetadataEntry& reg_config) {
  uint32_t reg_count = 0;
  for (const auto& it : reg_config.masks()) {
    reg_count += it.count();
  }

  for (uint32_t i = 0; i < mmios_.size(); i++) {
    auto offset = reg_config.base_address() - mmios_[i].base_address;
    if ((reg_config.base_address() >= mmios_[i].base_address) &&
        (offset / sizeof(T) + reg_count <= mmios_[i].locks.size())) {
      return i;
    }
  }

  return std::nullopt;
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
    mmios_.push_back(MmioInfo{
        .mmio = *std::move(tmp_mmio),
        .base_address = metadata.mmio()[i].base_address(),
        .locks = std::move(tmp_locks),
    });
  }

  // Create Registers
  for (auto& reg : metadata.registers()) {
    auto mmio_index = FindMmio(reg);
    if (!mmio_index.has_value()) {
      zxlogf(ERROR, "%s: No MMIO for Register %lu--base address 0x%08lx.", __func__, reg.id(),
             reg.base_address());
      return ZX_ERR_INTERNAL;
    }

    fbl::AllocChecker ac;
    std::unique_ptr<Register<T>> tmp_register(
        new (&ac) Register<T>(this->zxdev(), &mmios_[mmio_index.value()]));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    zx_device_prop_t props[] = {
        // TODO: cannot be 64 bits
        {BIND_REGISTER_ID, 0, static_cast<uint32_t>(reg.id())},
    };
    char name[20];
    snprintf(name, sizeof(name), "register-%lu", reg.id());
    auto status = tmp_register->DdkAdd(
        ddk::DeviceAddArgs(name).set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE).set_props(props));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: DdkAdd for %s failed %d", __func__, name, status);
      return status;
    }

    registers_.push_back(std::move(tmp_register));
    registers_.back()->Init(reg);
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
    zxlogf(ERROR, "%s: device_get_metadata_size failed %d", __FILE__, status);
    return status;
  }

  size_t actual;
  auto bytes = std::make_unique<uint8_t[]>(size);
  status = device_get_metadata(parent, DEVICE_METADATA_REGISTERS, bytes.get(), size, &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata failed %d", __FILE__, status);
    return status;
  }
  if (actual != size) {
    zxlogf(ERROR, "%s: device_get_metadata size error %d", __FILE__, status);
    return ZX_ERR_INTERNAL;
  }

  // Parse
  fidl::IncomingMessage<Metadata> decoded(bytes.get(), static_cast<uint32_t>(size), nullptr, 0);
  if (!decoded.ok() || (decoded.error() != nullptr)) {
    zxlogf(ERROR, "%s: Unable to parse metadata %s", __FILE__, decoded.error());
    return ZX_ERR_INTERNAL;
  }
  const auto& metadata = decoded.PrimaryObject();

  // Validate
  bool begin = true;
  ::llcpp::fuchsia::hardware::registers::Mask::Tag tag;
  for (const auto& reg : metadata->registers()) {
    if (begin) {
      tag = reg.masks().begin()->mask().which();
      begin = false;
    }

    if (reg.base_address() % kTagToBytes.at(tag)) {
      zxlogf(ERROR,
             "%s: Register with base address 0x%08lx does not start at the beginning of a register",
             __func__, reg.base_address());
      return ZX_ERR_INTERNAL;
    }

    for (const auto& mask : reg.masks()) {
      if (!mask.has_mask() || (mask.mask().which() != tag)) {
        zxlogf(ERROR, "%s: Width of registers don't match up.", __FILE__);
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
ZIRCON_DRIVER(registers, registers::driver_ops, "zircon", "0.1")
// clang-format on
