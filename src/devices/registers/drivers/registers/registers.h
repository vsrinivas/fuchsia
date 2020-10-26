// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_REGISTERS_DRIVERS_REGISTERS_REGISTERS_H_
#define SRC_DEVICES_REGISTERS_DRIVERS_REGISTERS_REGISTERS_H_

#include <fuchsia/hardware/registers/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/mmio/mmio.h>
#include <zircon/types.h>

#include <map>
#include <optional>
#include <vector>

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/registers.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>

namespace registers {

template <typename T>
class Register;
template <typename T>
using RegisterType = ddk::Device<Register<T>, ddk::Messageable, ddk::Unbindable>;

template <typename T>
class RegistersDevice;
template <typename T>
using RegistersDeviceType = ddk::Device<RegistersDevice<T>, ddk::Unbindable>;

struct MmioInfo {
  ddk::MmioBuffer mmio;
  uint64_t base_address;
  std::vector<fbl::Mutex> locks;
};

using ::llcpp::fuchsia::hardware::registers::Metadata;
using ::llcpp::fuchsia::hardware::registers::RegistersMetadataEntry;

template <typename T>
class Register : public ::llcpp::fuchsia::hardware::registers::Device::Interface,
                 public RegisterType<T>,
                 public ddk::RegistersProtocol<Register<T>, ddk::base_protocol>,
                 public fbl::RefCounted<Register<T>> {
 public:
  explicit Register(zx_device_t* device, MmioInfo* mmio)
      : RegisterType<T>(device), mmio_(mmio), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  zx_status_t Init(const RegistersMetadataEntry& config);

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    ::llcpp::fuchsia::hardware::registers::Device::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }
  void DdkUnbind(ddk::UnbindTxn txn) {
    loop_.Shutdown();
    txn.Reply();
  }
  void DdkRelease() { delete this; }

  void RegistersConnect(zx::channel chan);

  void ReadRegister8(uint64_t address, uint8_t mask, ReadRegister8Completer::Sync& completer) {
    ReadRegister(address, mask, completer);
  }
  void ReadRegister16(uint64_t address, uint16_t mask, ReadRegister16Completer::Sync& completer) {
    ReadRegister(address, mask, completer);
  }
  void ReadRegister32(uint64_t address, uint32_t mask, ReadRegister32Completer::Sync& completer) {
    ReadRegister(address, mask, completer);
  }
  void ReadRegister64(uint64_t address, uint64_t mask, ReadRegister64Completer::Sync& completer) {
    ReadRegister(address, mask, completer);
  }
  void WriteRegister8(uint64_t address, uint8_t mask, uint8_t value,
                      WriteRegister8Completer::Sync& completer) {
    WriteRegister(address, mask, value, completer);
  }
  void WriteRegister16(uint64_t address, uint16_t mask, uint16_t value,
                       WriteRegister16Completer::Sync& completer) {
    WriteRegister(address, mask, value, completer);
  }
  void WriteRegister32(uint64_t address, uint32_t mask, uint32_t value,
                       WriteRegister32Completer::Sync& completer) {
    WriteRegister(address, mask, value, completer);
  }
  void WriteRegister64(uint64_t address, uint64_t mask, uint64_t value,
                       WriteRegister64Completer::Sync& completer) {
    WriteRegister(address, mask, value, completer);
  }

 private:
  template <typename Ty, typename Completer>
  void ReadRegister(uint64_t address, Ty mask, Completer& completer) {
    if constexpr (!std::is_same_v<T, Ty>) {
      completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
      return;
    }
    T val;
    // Need cast to compile
    auto status = ReadRegister(address, static_cast<T>(mask), &val);
    if (status == ZX_OK) {
      // Need cast to compile
      completer.ReplySuccess(static_cast<Ty>(val));
    } else {
      completer.ReplyError(status);
    }
  }

  template <typename Ty, typename Completer>
  void WriteRegister(uint64_t address, Ty mask, Ty value, Completer& completer) {
    if constexpr (!std::is_same_v<T, Ty>) {
      completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
      return;
    }
    // Need cast to compile
    auto status = WriteRegister(address, static_cast<T>(mask), static_cast<T>(value));
    if (status == ZX_OK) {
      completer.ReplySuccess();
    } else {
      completer.ReplyError(status);
    }
  }

  zx_status_t ReadRegister(uint64_t address, T mask, T* out_value);
  zx_status_t WriteRegister(uint64_t address, T mask, T value);

  bool VerifyMask(T mask, uint64_t register_offset);

  MmioInfo* mmio_;
  uint64_t id_;
  uint64_t base_address_;
  std::map<uint64_t, T> masks_;  // map of reg count upper bounds (inclusive) to mask

  async::Loop loop_;
  bool loop_started_ = false;
};

template <typename T>
class RegistersDevice : public RegistersDeviceType<T> {
 public:
  static zx_status_t Create(zx_device_t* parent, Metadata metadata);

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 private:
  template <typename U>
  friend class FakeRegistersDevice;

  explicit RegistersDevice(zx_device_t* parent) : RegistersDeviceType<T>(parent) {}

  zx_status_t Init(zx_device_t* parent, Metadata metadata);
  // For unit tests
  zx_status_t Init(std::vector<MmioInfo> mmios) {
    mmios_ = std::move(mmios);
    return ZX_OK;
  }
  // For unit tests
  std::shared_ptr<::llcpp::fuchsia::hardware::registers::Device::SyncClient> AddRegister(
      uint32_t mmio_index, RegistersMetadataEntry& config) {
    registers_.push_back(std::make_unique<Register<T>>(nullptr, &mmios_[mmio_index]));
    registers_.back()->Init(config);

    zx::channel client_end, server_end;
    if (zx::channel::create(0, &client_end, &server_end) != ZX_OK) {
      return nullptr;
    }
    registers_.back()->RegistersConnect(std::move(server_end));
    return std::make_shared<::llcpp::fuchsia::hardware::registers::Device::SyncClient>(
        std::move(client_end));
  }
  std::optional<uint32_t> FindMmio(const RegistersMetadataEntry& reg_config);

  std::vector<MmioInfo> mmios_;
  std::vector<std::unique_ptr<Register<T>>> registers_;
};

}  // namespace registers

#endif  // SRC_DEVICES_REGISTERS_DRIVERS_REGISTERS_REGISTERS_H_
