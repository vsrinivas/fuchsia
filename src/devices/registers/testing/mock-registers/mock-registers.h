// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_REGISTERS_TESTING_MOCK_REGISTERS_MOCK_REGISTERS_H_
#define SRC_DEVICES_REGISTERS_TESTING_MOCK_REGISTERS_MOCK_REGISTERS_H_

#include <fuchsia/hardware/registers/cpp/banjo.h>
#include <fuchsia/hardware/registers/llcpp/fidl.h>

#include <map>
#include <queue>

namespace mock_registers {

// Mock Registers. FIDL implementation.
class MockRegisters : public fidl::WireServer<fuchsia_hardware_registers::Device> {
 public:
  explicit MockRegisters(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}
  ~MockRegisters() {}

  // Manage the Fake FIDL Message Loop
  void Init(zx::channel remote) {
    fidl::BindServer(dispatcher_,
                     fidl::ServerEnd<fuchsia_hardware_registers::Device>(std::move(remote)), this);
  }

  template <typename T>
  void ExpectRead(uint64_t offset, T mask, T value) {
    static_assert(std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> ||
                  std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>);
    GetExpectRead<T>()[offset].push(std::pair<T, T>(mask, value));
  }

  template <typename T>
  void ExpectWrite(uint64_t offset, T mask, T value) {
    static_assert(std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> ||
                  std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>);
    GetExpectWrite<T>()[offset].push(std::pair<T, T>(mask, value));
  }

  inline zx_status_t VerifyAll();

 private:
  // Implement Registers FIDL Protocol.
  void ReadRegister8(ReadRegister8RequestView request,
                     ReadRegister8Completer::Sync& completer) override {
    ReadRegister(request->offset, request->mask, completer);
  }
  void ReadRegister16(ReadRegister16RequestView request,
                      ReadRegister16Completer::Sync& completer) override {
    ReadRegister(request->offset, request->mask, completer);
  }
  void ReadRegister32(ReadRegister32RequestView request,
                      ReadRegister32Completer::Sync& completer) override {
    ReadRegister(request->offset, request->mask, completer);
  }
  void ReadRegister64(ReadRegister64RequestView request,
                      ReadRegister64Completer::Sync& completer) override {
    ReadRegister(request->offset, request->mask, completer);
  }
  void WriteRegister8(WriteRegister8RequestView request,
                      WriteRegister8Completer::Sync& completer) override {
    WriteRegister(request->offset, request->mask, request->value, completer);
  }
  void WriteRegister16(WriteRegister16RequestView request,
                       WriteRegister16Completer::Sync& completer) override {
    WriteRegister(request->offset, request->mask, request->value, completer);
  }
  void WriteRegister32(WriteRegister32RequestView request,
                       WriteRegister32Completer::Sync& completer) override {
    WriteRegister(request->offset, request->mask, request->value, completer);
  }
  void WriteRegister64(WriteRegister64RequestView request,
                       WriteRegister64Completer::Sync& completer) override {
    WriteRegister(request->offset, request->mask, request->value, completer);
  }

  // Helper functions for FIDL
  template <typename T, typename Completer>
  void ReadRegister(uint64_t offset, T mask, Completer& completer) {
    auto& expect_read = GetExpectRead<T>()[offset];
    if (expect_read.empty()) {
      completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
      return;
    }
    auto value = expect_read.front();
    expect_read.pop();
    if (value.first == mask) {
      completer.ReplySuccess(static_cast<T>(value.second));
    } else {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
    }
  }

  template <typename T, typename Completer>
  void WriteRegister(uint64_t offset, T mask, T value, Completer& completer) {
    auto& expect_write = GetExpectWrite<T>()[offset];
    if (expect_write.empty()) {
      completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
      return;
    }
    auto expected_value = expect_write.front();
    expect_write.pop();
    if ((expected_value.first == mask) && (expected_value.second == value)) {
      completer.ReplySuccess();
    } else {
      completer.ReplyError(ZX_ERR_INVALID_ARGS);
    }
  }

  // Helper functions to get queues.
  template <typename T>
  auto& GetExpectRead() {
    if constexpr (!std::is_same_v<T, uint64_t>) {
      return expect_read64;
    } else if constexpr (!std::is_same_v<T, uint32_t>) {
      return expect_read32;
    } else if constexpr (!std::is_same_v<T, uint16_t>) {
      return expect_read16;
    } else if constexpr (!std::is_same_v<T, uint8_t>) {
      return expect_read8;
    }
  }

  template <typename T>
  auto& GetExpectWrite() {
    if constexpr (!std::is_same_v<T, uint64_t>) {
      return expect_write64;
    } else if constexpr (!std::is_same_v<T, uint32_t>) {
      return expect_write32;
    } else if constexpr (!std::is_same_v<T, uint16_t>) {
      return expect_write16;
    } else if constexpr (!std::is_same_v<T, uint8_t>) {
      return expect_write8;
    }
  }

  async_dispatcher_t* dispatcher_;

  std::map<uint64_t, std::queue<std::pair<uint8_t, uint8_t>>> expect_read8;
  std::map<uint64_t, std::queue<std::pair<uint16_t, uint16_t>>> expect_read16;
  std::map<uint64_t, std::queue<std::pair<uint32_t, uint32_t>>> expect_read32;
  std::map<uint64_t, std::queue<std::pair<uint64_t, uint64_t>>> expect_read64;
  std::map<uint64_t, std::queue<std::pair<uint8_t, uint8_t>>> expect_write8;
  std::map<uint64_t, std::queue<std::pair<uint16_t, uint16_t>>> expect_write16;
  std::map<uint64_t, std::queue<std::pair<uint32_t, uint32_t>>> expect_write32;
  std::map<uint64_t, std::queue<std::pair<uint64_t, uint64_t>>> expect_write64;
};

// Mock Registers Device implementing Banjo protocol to connect to FIDL implementation.
class MockRegistersDevice : public ddk::RegistersProtocol<MockRegistersDevice> {
 public:
  MockRegistersDevice(async_dispatcher_t* dispatcher)
      : proto_({&registers_protocol_ops_, this}), fidl_service_(dispatcher) {}

  void RegistersConnect(zx::channel chan) { fidl_service_.Init(std::move(chan)); }

  const registers_protocol_t* proto() const { return &proto_; }
  MockRegisters* fidl_service() { return &fidl_service_; }

 private:
  registers_protocol_t proto_;
  MockRegisters fidl_service_;
};

zx_status_t MockRegisters::VerifyAll() {
  for (const auto& expect_read : GetExpectRead<uint8_t>()) {
    if (!expect_read.second.empty()) {
      return ZX_ERR_INTERNAL;
    }
  }
  for (const auto& expect_read : GetExpectRead<uint16_t>()) {
    if (!expect_read.second.empty()) {
      return ZX_ERR_INTERNAL;
    }
  }
  for (const auto& expect_read : GetExpectRead<uint32_t>()) {
    if (!expect_read.second.empty()) {
      return ZX_ERR_INTERNAL;
    }
  }
  for (const auto& expect_read : GetExpectRead<uint64_t>()) {
    if (!expect_read.second.empty()) {
      return ZX_ERR_INTERNAL;
    }
  }

  for (const auto& expect_write : GetExpectWrite<uint8_t>()) {
    if (!expect_write.second.empty()) {
      return ZX_ERR_INTERNAL;
    }
  }
  for (const auto& expect_write : GetExpectWrite<uint16_t>()) {
    if (!expect_write.second.empty()) {
      return ZX_ERR_INTERNAL;
    }
  }
  for (const auto& expect_write : GetExpectWrite<uint32_t>()) {
    if (!expect_write.second.empty()) {
      return ZX_ERR_INTERNAL;
    }
  }
  for (const auto& expect_write : GetExpectWrite<uint64_t>()) {
    if (!expect_write.second.empty()) {
      return ZX_ERR_INTERNAL;
    }
  }

  return ZX_OK;
}

}  // namespace mock_registers

#endif  // SRC_DEVICES_REGISTERS_TESTING_MOCK_REGISTERS_MOCK_REGISTERS_H_
