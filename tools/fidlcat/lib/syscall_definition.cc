// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/system/public/zircon/errors.h>
#include <zircon/system/public/zircon/syscalls/port.h>

#include <cstdint>
#include <memory>

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

class ZxChannelCallArgs {
 public:
  static const uint8_t* wr_bytes(const zx_channel_call_args_t* from) {
    return reinterpret_cast<const uint8_t*>(from->wr_bytes);
  }
  static const zx_handle_t* wr_handles(const zx_channel_call_args_t* from) {
    return from->wr_handles;
  }
  static const uint8_t* rd_bytes(const zx_channel_call_args_t* from) {
    return reinterpret_cast<const uint8_t*>(from->rd_bytes);
  }
  static const zx_handle_t* rd_handles(const zx_channel_call_args_t* from) {
    return from->rd_handles;
  }
  static uint32_t wr_num_bytes(const zx_channel_call_args_t* from) { return from->wr_num_bytes; }
  static uint32_t wr_num_handles(const zx_channel_call_args_t* from) {
    return from->wr_num_handles;
  }
  static uint32_t rd_num_bytes(const zx_channel_call_args_t* from) { return from->rd_num_bytes; }
  static uint32_t rd_num_handles(const zx_channel_call_args_t* from) {
    return from->rd_num_handles;
  }
};

class ZxPacketUser : public Class<zx_packet_user_t> {
 public:
  static const ZxPacketUser* GetClass();

  static std::pair<const uint64_t*, int> u64(const zx_packet_user_t* from) {
    return std::make_pair(reinterpret_cast<const uint64_t*>(from->u64),
                          sizeof(from->u64) / sizeof(uint64_t));
  }
  static std::pair<const uint32_t*, int> u32(const zx_packet_user_t* from) {
    return std::make_pair(reinterpret_cast<const uint32_t*>(from->u32),
                          sizeof(from->u32) / sizeof(uint32_t));
  }
  static std::pair<const uint16_t*, int> u16(const zx_packet_user_t* from) {
    return std::make_pair(reinterpret_cast<const uint16_t*>(from->u16),
                          sizeof(from->u16) / sizeof(uint16_t));
  }
  static std::pair<const uint8_t*, int> c8(const zx_packet_user_t* from) {
    return std::make_pair(reinterpret_cast<const uint8_t*>(from->c8),
                          sizeof(from->c8) / sizeof(uint8_t));
  }

 private:
  ZxPacketUser() : Class("zx_packet_user_t") {
    AddField(std::make_unique<ClassField<zx_packet_user_t, std::pair<const uint64_t*, int>>>(
        "u64", SyscallType::kUint64Array, u64));
    AddField(std::make_unique<ClassField<zx_packet_user_t, std::pair<const uint32_t*, int>>>(
        "u32", SyscallType::kUint32Array, u32));
    AddField(std::make_unique<ClassField<zx_packet_user_t, std::pair<const uint16_t*, int>>>(
        "u16", SyscallType::kUint16Array, u16));
    AddField(std::make_unique<ClassField<zx_packet_user_t, std::pair<const uint8_t*, int>>>(
        "u8", SyscallType::kUint8Array, c8));
  }
  ZxPacketUser(const ZxPacketUser&) = delete;
  ZxPacketUser& operator=(const ZxPacketUser&) = delete;
  static ZxPacketUser* instance_;
};

ZxPacketUser* ZxPacketUser::instance_ = nullptr;

const ZxPacketUser* ZxPacketUser::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketUser;
  }
  return instance_;
}

class ZxPacketSignal : public Class<zx_packet_signal_t> {
 public:
  static const ZxPacketSignal* GetClass();

  static zx_signals_t trigger(const zx_packet_signal_t* from) { return from->trigger; }
  static zx_signals_t observed(const zx_packet_signal_t* from) { return from->observed; }
  static uint64_t count(const zx_packet_signal_t* from) { return from->count; }
  static uint64_t timestamp(const zx_packet_signal_t* from) { return from->timestamp; }
  static uint64_t reserved1(const zx_packet_signal_t* from) { return from->reserved1; }

 private:
  ZxPacketSignal() : Class("zx_packet_signal_t") {
    AddField(std::make_unique<ClassField<zx_packet_signal_t, zx_signals_t>>(
        "trigger", SyscallType::kSignals, trigger));
    AddField(std::make_unique<ClassField<zx_packet_signal_t, zx_signals_t>>(
        "observed", SyscallType::kSignals, observed));
    AddField(std::make_unique<ClassField<zx_packet_signal_t, uint64_t>>(
        "count", SyscallType::kUint64, count));
    AddField(std::make_unique<ClassField<zx_packet_signal_t, uint64_t>>(
        "timestamp", SyscallType::kTime, timestamp));
    AddField(std::make_unique<ClassField<zx_packet_signal_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
  }
  ZxPacketSignal(const ZxPacketSignal&) = delete;
  ZxPacketSignal& operator=(const ZxPacketSignal&) = delete;
  static ZxPacketSignal* instance_;
};

ZxPacketSignal* ZxPacketSignal::instance_ = nullptr;

const ZxPacketSignal* ZxPacketSignal::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketSignal;
  }
  return instance_;
}

class ZxPacketException : public Class<zx_packet_exception_t> {
 public:
  static const ZxPacketException* GetClass();

  static uint64_t pid(const zx_packet_exception_t* from) { return from->pid; }
  static uint64_t tid(const zx_packet_exception_t* from) { return from->tid; }
  static uint64_t reserved0(const zx_packet_exception_t* from) { return from->reserved0; }
  static uint64_t reserved1(const zx_packet_exception_t* from) { return from->reserved1; }

 private:
  ZxPacketException() : Class("zx_packet_exception_t") {
    AddField(std::make_unique<ClassField<zx_packet_exception_t, uint64_t>>(
        "pid", SyscallType::kUint64, pid));
    AddField(std::make_unique<ClassField<zx_packet_exception_t, uint64_t>>(
        "tid", SyscallType::kUint64, tid));
    AddField(std::make_unique<ClassField<zx_packet_exception_t, uint64_t>>(
        "reserved0", SyscallType::kUint64, reserved0));
    AddField(std::make_unique<ClassField<zx_packet_exception_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
  }
  ZxPacketException(const ZxPacketException&) = delete;
  ZxPacketException& operator=(const ZxPacketException&) = delete;
  static ZxPacketException* instance_;
};

ZxPacketException* ZxPacketException::instance_ = nullptr;

const ZxPacketException* ZxPacketException::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketException;
  }
  return instance_;
}

class ZxPacketGuestBell : public Class<zx_packet_guest_bell_t> {
 public:
  static const ZxPacketGuestBell* GetClass();

  static zx_gpaddr_t addr(const zx_packet_guest_bell_t* from) { return from->addr; }
  static uint64_t reserved0(const zx_packet_guest_bell_t* from) { return from->reserved0; }
  static uint64_t reserved1(const zx_packet_guest_bell_t* from) { return from->reserved1; }
  static uint64_t reserved2(const zx_packet_guest_bell_t* from) { return from->reserved2; }

 private:
  ZxPacketGuestBell() : Class("zx_packet_guest_bell_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_bell_t, zx_gpaddr_t>>(
        "addr", SyscallType::kGpAddr, addr));
    AddField(std::make_unique<ClassField<zx_packet_guest_bell_t, uint64_t>>(
        "reserved0", SyscallType::kUint64, reserved0));
    AddField(std::make_unique<ClassField<zx_packet_guest_bell_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
    AddField(std::make_unique<ClassField<zx_packet_guest_bell_t, uint64_t>>(
        "reserved2", SyscallType::kUint64, reserved2));
  }
  ZxPacketGuestBell(const ZxPacketGuestBell&) = delete;
  ZxPacketGuestBell& operator=(const ZxPacketGuestBell&) = delete;
  static ZxPacketGuestBell* instance_;
};

ZxPacketGuestBell* ZxPacketGuestBell::instance_ = nullptr;

const ZxPacketGuestBell* ZxPacketGuestBell::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestBell;
  }
  return instance_;
}

class ZxPacketGuestMemAArch64 : public Class<zx_packet_guest_mem_aarch64_t> {
 public:
  static const ZxPacketGuestMemAArch64* GetClass();

  static zx_gpaddr_t addr(const zx_packet_guest_mem_aarch64_t* from) { return from->addr; }
  static uint8_t access_size(const zx_packet_guest_mem_aarch64_t* from) {
    return from->access_size;
  }
  static bool sign_extend(const zx_packet_guest_mem_aarch64_t* from) { return from->sign_extend; }
  static uint8_t xt(const zx_packet_guest_mem_aarch64_t* from) { return from->xt; }
  static bool read(const zx_packet_guest_mem_aarch64_t* from) { return from->read; }
  static uint64_t data(const zx_packet_guest_mem_aarch64_t* from) { return from->data; }
  static uint64_t reserved(const zx_packet_guest_mem_aarch64_t* from) { return from->reserved; }

 private:
  ZxPacketGuestMemAArch64() : Class("zx_packet_guest_mem_aarch64_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, zx_gpaddr_t>>(
        "addr", SyscallType::kGpAddr, addr));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, uint8_t>>(
        "access_size", SyscallType::kUint8, access_size));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, bool>>(
        "sign_extend", SyscallType::kBool, sign_extend));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, uint8_t>>(
        "xt", SyscallType::kUint8, xt));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, bool>>(
        "read", SyscallType::kBool, read));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, uint64_t>>(
        "data", SyscallType::kUint64, data));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, uint64_t>>(
        "reserved", SyscallType::kUint64, reserved));
  }
  ZxPacketGuestMemAArch64(const ZxPacketGuestMemAArch64&) = delete;
  ZxPacketGuestMemAArch64& operator=(const ZxPacketGuestMemAArch64&) = delete;
  static ZxPacketGuestMemAArch64* instance_;
};

ZxPacketGuestMemAArch64* ZxPacketGuestMemAArch64::instance_ = nullptr;

const ZxPacketGuestMemAArch64* ZxPacketGuestMemAArch64::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestMemAArch64;
  }
  return instance_;
}

class ZxPacketGuestMemX86 : public Class<zx_packet_guest_mem_x86_t> {
 public:
  static const ZxPacketGuestMemX86* GetClass();

  static zx_gpaddr_t addr(const zx_packet_guest_mem_x86_t* from) { return from->addr; }
  static uint8_t inst_len(const zx_packet_guest_mem_x86_t* from) { return from->inst_len; }
  static std::pair<const uint8_t*, int> inst_buf(const zx_packet_guest_mem_x86_t* from) {
    return std::make_pair(reinterpret_cast<const uint8_t*>(from->inst_buf),
                          sizeof(from->inst_buf) / sizeof(uint8_t));
  }
  static uint8_t default_operand_size(const zx_packet_guest_mem_x86_t* from) {
    return from->default_operand_size;
  }
  static std::pair<const uint8_t*, int> reserved(const zx_packet_guest_mem_x86_t* from) {
    return std::make_pair(reinterpret_cast<const uint8_t*>(from->reserved),
                          sizeof(from->reserved) / sizeof(uint8_t));
  }

 private:
  ZxPacketGuestMemX86() : Class("zx_packet_guest_mem_x86_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_x86_t, zx_gpaddr_t>>(
        "addr", SyscallType::kGpAddr, addr));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_x86_t, uint8_t>>(
        "inst_len", SyscallType::kUint8, inst_len));
    AddField(
        std::make_unique<ClassField<zx_packet_guest_mem_x86_t, std::pair<const uint8_t*, int>>>(
            "inst_buf", SyscallType::kUint8Array, inst_buf));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_x86_t, uint8_t>>(
        "default_operand_size", SyscallType::kUint8, default_operand_size));
    AddField(
        std::make_unique<ClassField<zx_packet_guest_mem_x86_t, std::pair<const uint8_t*, int>>>(
            "reserved", SyscallType::kUint8Array, reserved));
  }
  ZxPacketGuestMemX86(const ZxPacketGuestMemX86&) = delete;
  ZxPacketGuestMemX86& operator=(const ZxPacketGuestMemX86&) = delete;
  static ZxPacketGuestMemX86* instance_;
};

ZxPacketGuestMemX86* ZxPacketGuestMemX86::instance_ = nullptr;

const ZxPacketGuestMemX86* ZxPacketGuestMemX86::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestMemX86;
  }
  return instance_;
}

class ZxPacketGuestIo : public Class<zx_packet_guest_io_t> {
 public:
  static const ZxPacketGuestIo* GetClass();

  static uint16_t port(const zx_packet_guest_io_t* from) { return from->port; }
  static uint8_t access_size(const zx_packet_guest_io_t* from) { return from->access_size; }
  static bool input(const zx_packet_guest_io_t* from) { return from->input; }
  static uint8_t u8(const zx_packet_guest_io_t* from) { return from->u8; }
  static uint16_t u16(const zx_packet_guest_io_t* from) { return from->u16; }
  static uint32_t u32(const zx_packet_guest_io_t* from) { return from->u32; }
  static std::pair<const uint8_t*, int> data(const zx_packet_guest_io_t* from) {
    return std::make_pair(reinterpret_cast<const uint8_t*>(from->data),
                          sizeof(from->data) / sizeof(uint8_t));
  }
  static uint64_t reserved0(const zx_packet_guest_io_t* from) { return from->reserved0; }
  static uint64_t reserved1(const zx_packet_guest_io_t* from) { return from->reserved1; }
  static uint64_t reserved2(const zx_packet_guest_io_t* from) { return from->reserved2; }

 private:
  ZxPacketGuestIo() : Class("zx_packet_guest_io_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint16_t>>(
        "port", SyscallType::kUint16, port));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint8_t>>(
        "access_size", SyscallType::kUint8, access_size));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, bool>>("input", SyscallType::kBool,
                                                                      input));
    AddField(
        std::make_unique<ClassField<zx_packet_guest_io_t, uint8_t>>("u8", SyscallType::kUint8, u8));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint16_t>>(
        "u16", SyscallType::kUint16, u16));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint32_t>>(
        "u32", SyscallType::kUint32, u32));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, std::pair<const uint8_t*, int>>>(
        "data", SyscallType::kUint8Array, data));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint64_t>>(
        "reserved0", SyscallType::kUint64, reserved0));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint64_t>>(
        "reserved2", SyscallType::kUint64, reserved2));
  }
  ZxPacketGuestIo(const ZxPacketGuestIo&) = delete;
  ZxPacketGuestIo& operator=(const ZxPacketGuestIo&) = delete;
  static ZxPacketGuestIo* instance_;
};

ZxPacketGuestIo* ZxPacketGuestIo::instance_ = nullptr;

const ZxPacketGuestIo* ZxPacketGuestIo::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestIo;
  }
  return instance_;
}

// This is extracted from zx_packet_vcpu from zircon/system/public/zircon/syscalls/port.h
struct zx_packet_guest_vcpu_interrupt {
  uint64_t mask;
  uint8_t vector;
};
using zx_packet_guest_vcpu_interrupt_t = struct zx_packet_guest_vcpu_interrupt;

class ZxPacketGuestVcpuInterrupt : public Class<zx_packet_guest_vcpu_interrupt_t> {
 public:
  static const ZxPacketGuestVcpuInterrupt* GetClass();

  static uint64_t mask(const zx_packet_guest_vcpu_interrupt_t* from) { return from->mask; }
  static uint8_t vector(const zx_packet_guest_vcpu_interrupt_t* from) { return from->vector; }

 private:
  ZxPacketGuestVcpuInterrupt() : Class("zx_packet_guest_vcpu_interrupt_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_interrupt_t, uint64_t>>(
        "mask", SyscallType::kUint64, mask));
    AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_interrupt_t, uint8_t>>(
        "vector", SyscallType::kUint8, vector));
  }
  ZxPacketGuestVcpuInterrupt(const ZxPacketGuestVcpuInterrupt&) = delete;
  ZxPacketGuestVcpuInterrupt& operator=(const ZxPacketGuestVcpuInterrupt&) = delete;
  static ZxPacketGuestVcpuInterrupt* instance_;
};

ZxPacketGuestVcpuInterrupt* ZxPacketGuestVcpuInterrupt::instance_ = nullptr;

const ZxPacketGuestVcpuInterrupt* ZxPacketGuestVcpuInterrupt::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestVcpuInterrupt;
  }
  return instance_;
}

// This is extracted from zx_packet_vcpu from zircon/system/public/zircon/syscalls/port.h
struct zx_packet_guest_vcpu_startup {
  uint64_t id;
  zx_gpaddr_t entry;
};
using zx_packet_guest_vcpu_startup_t = struct zx_packet_guest_vcpu_startup;

class ZxPacketGuestVcpuStartup : public Class<zx_packet_guest_vcpu_startup_t> {
 public:
  static const ZxPacketGuestVcpuStartup* GetClass();

  static uint64_t id(const zx_packet_guest_vcpu_startup_t* from) { return from->id; }
  static zx_gpaddr_t entry(const zx_packet_guest_vcpu_startup_t* from) { return from->entry; }

 private:
  ZxPacketGuestVcpuStartup() : Class("zx_packet_guest_vcpu_startup_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_startup_t, uint64_t>>(
        "id", SyscallType::kUint64, id));
    AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_startup_t, zx_gpaddr_t>>(
        "entry", SyscallType::kGpAddr, entry));
  }
  ZxPacketGuestVcpuStartup(const ZxPacketGuestVcpuStartup&) = delete;
  ZxPacketGuestVcpuStartup& operator=(const ZxPacketGuestVcpuStartup&) = delete;
  static ZxPacketGuestVcpuStartup* instance_;
};

ZxPacketGuestVcpuStartup* ZxPacketGuestVcpuStartup::instance_ = nullptr;

const ZxPacketGuestVcpuStartup* ZxPacketGuestVcpuStartup::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestVcpuStartup;
  }
  return instance_;
}

class ZxPacketGuestVcpu : public Class<zx_packet_guest_vcpu_t> {
 public:
  static const ZxPacketGuestVcpu* GetClass();

  static const zx_packet_guest_vcpu_interrupt_t* interrupt(const zx_packet_guest_vcpu_t* from) {
    return reinterpret_cast<const zx_packet_guest_vcpu_interrupt_t*>(&from->interrupt);
  }
  static const zx_packet_guest_vcpu_startup_t* startup(const zx_packet_guest_vcpu_t* from) {
    return reinterpret_cast<const zx_packet_guest_vcpu_startup_t*>(&from->startup);
  }
  static uint8_t type(const zx_packet_guest_vcpu_t* from) { return from->type; }
  static uint64_t reserved(const zx_packet_guest_vcpu_t* from) { return from->reserved; }

 private:
  ZxPacketGuestVcpu() : Class("zx_packet_guest_vcpu_t") {
    auto type_field = AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_t, uint8_t>>(
        "type", SyscallType::kPacketGuestVcpuType, type));
    AddField(
        std::make_unique<ClassClassField<zx_packet_guest_vcpu_t, zx_packet_guest_vcpu_interrupt_t>>(
            "interrupt", interrupt, ZxPacketGuestVcpuInterrupt::GetClass()))
        ->DisplayIfEqual(type_field, uint8_t(ZX_PKT_GUEST_VCPU_INTERRUPT));
    AddField(
        std::make_unique<ClassClassField<zx_packet_guest_vcpu_t, zx_packet_guest_vcpu_startup_t>>(
            "startup", startup, ZxPacketGuestVcpuStartup::GetClass()))
        ->DisplayIfEqual(type_field, uint8_t(ZX_PKT_GUEST_VCPU_STARTUP));
    AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_t, uint64_t>>(
        "reserved", SyscallType::kUint64, reserved));
  }
  ZxPacketGuestVcpu(const ZxPacketGuestVcpu&) = delete;
  ZxPacketGuestVcpu& operator=(const ZxPacketGuestVcpu&) = delete;
  static ZxPacketGuestVcpu* instance_;
};

ZxPacketGuestVcpu* ZxPacketGuestVcpu::instance_ = nullptr;

const ZxPacketGuestVcpu* ZxPacketGuestVcpu::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestVcpu;
  }
  return instance_;
}

class ZxPacketInterrupt : public Class<zx_packet_interrupt_t> {
 public:
  static const ZxPacketInterrupt* GetClass();

  static zx_time_t timestamp(const zx_packet_interrupt_t* from) { return from->timestamp; }
  static uint64_t reserved0(const zx_packet_interrupt_t* from) { return from->reserved0; }
  static uint64_t reserved1(const zx_packet_interrupt_t* from) { return from->reserved1; }
  static uint64_t reserved2(const zx_packet_interrupt_t* from) { return from->reserved2; }

 private:
  ZxPacketInterrupt() : Class("zx_packet_interrupt_t") {
    AddField(std::make_unique<ClassField<zx_packet_interrupt_t, zx_time_t>>(
        "timestamp", SyscallType::kTime, timestamp));
    AddField(std::make_unique<ClassField<zx_packet_interrupt_t, uint64_t>>(
        "reserved0", SyscallType::kUint64, reserved0));
    AddField(std::make_unique<ClassField<zx_packet_interrupt_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
    AddField(std::make_unique<ClassField<zx_packet_interrupt_t, uint64_t>>(
        "reserved2", SyscallType::kUint64, reserved2));
  }
  ZxPacketInterrupt(const ZxPacketInterrupt&) = delete;
  ZxPacketInterrupt& operator=(const ZxPacketInterrupt&) = delete;
  static ZxPacketInterrupt* instance_;
};

ZxPacketInterrupt* ZxPacketInterrupt::instance_ = nullptr;

const ZxPacketInterrupt* ZxPacketInterrupt::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketInterrupt;
  }
  return instance_;
}

class ZxPacketPageRequest : public Class<zx_packet_page_request_t> {
 public:
  static const ZxPacketPageRequest* GetClass();

  static uint16_t command(const zx_packet_page_request_t* from) { return from->command; }
  static uint16_t flags(const zx_packet_page_request_t* from) { return from->flags; }
  static uint32_t reserved0(const zx_packet_page_request_t* from) { return from->reserved0; }
  static uint64_t offset(const zx_packet_page_request_t* from) { return from->offset; }
  static uint64_t length(const zx_packet_page_request_t* from) { return from->length; }
  static uint64_t reserved1(const zx_packet_page_request_t* from) { return from->reserved1; }

 private:
  ZxPacketPageRequest() : Class("zx_packet_page_request_t") {
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint16_t>>(
        "command", SyscallType::kPacketPageRequestCommand, command));
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint16_t>>(
        "flags", SyscallType::kUint16, flags));
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint32_t>>(
        "reserved0", SyscallType::kUint32, reserved0));
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint64_t>>(
        "offset", SyscallType::kUint64, offset));
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint64_t>>(
        "length", SyscallType::kUint64, length));
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
  }
  ZxPacketPageRequest(const ZxPacketPageRequest&) = delete;
  ZxPacketPageRequest& operator=(const ZxPacketPageRequest&) = delete;
  static ZxPacketPageRequest* instance_;
};

ZxPacketPageRequest* ZxPacketPageRequest::instance_ = nullptr;

const ZxPacketPageRequest* ZxPacketPageRequest::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketPageRequest;
  }
  return instance_;
}

class ZxPortPacket : public Class<zx_port_packet_t> {
 public:
  static const ZxPortPacket* GetClass();

  static uint64_t key(const zx_port_packet_t* from) { return from->key; }
  static uint32_t type(const zx_port_packet_t* from) { return from->type; }
  static zx_status_t status(const zx_port_packet_t* from) { return from->status; }
  static const zx_packet_user_t* user(const zx_port_packet_t* from) { return &from->user; }
  static const zx_packet_signal_t* signal(const zx_port_packet_t* from) { return &from->signal; }
  static const zx_packet_exception_t* exception(const zx_port_packet_t* from) {
    return &from->exception;
  }
  static const zx_packet_guest_bell_t* guest_bell(const zx_port_packet_t* from) {
    return &from->guest_bell;
  }
  static const zx_packet_guest_mem_aarch64_t* guest_mem_aarch64(const zx_port_packet_t* from) {
    return reinterpret_cast<const zx_packet_guest_mem_aarch64_t*>(&from->guest_mem);
  }
  static const zx_packet_guest_mem_x86_t* guest_mem_x86(const zx_port_packet_t* from) {
    return reinterpret_cast<const zx_packet_guest_mem_x86_t*>(&from->guest_mem);
  }
  static const zx_packet_guest_io_t* guest_io(const zx_port_packet_t* from) {
    return &from->guest_io;
  }
  static const zx_packet_guest_vcpu_t* guest_vcpu(const zx_port_packet_t* from) {
    return &from->guest_vcpu;
  }
  static const zx_packet_interrupt_t* interrupt(const zx_port_packet_t* from) {
    return &from->interrupt;
  }
  static const zx_packet_page_request_t* page_request(const zx_port_packet_t* from) {
    return &from->page_request;
  }

  static constexpr uint32_t kExceptionMask = 0xff;

 private:
  ZxPortPacket() : Class("zx_port_packet_t") {
    AddField(
        std::make_unique<ClassField<zx_port_packet_t, uint64_t>>("key", SyscallType::kUint64, key));
    auto type_field = AddField(std::make_unique<ClassField<zx_port_packet_t, uint32_t>>(
        "type", SyscallType::kPortPacketType, type));
    AddField(std::make_unique<ClassField<zx_port_packet_t, zx_status_t>>(
        "status", SyscallType::kStatus, status));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_user_t>>(
                 "user", user, ZxPacketUser::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_USER));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_signal_t>>(
                 "signal", signal, ZxPacketSignal::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_SIGNAL_ONE));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_signal_t>>(
                 "signal", signal, ZxPacketSignal::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_SIGNAL_REP));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_exception_t>>(
                 "exception", exception, ZxPacketException::GetClass()))
        ->DisplayIfMaskedEqual(type_field, kExceptionMask, uint32_t(ZX_PKT_TYPE_EXCEPTION(0)));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_bell_t>>(
                 "guest_bell", guest_bell, ZxPacketGuestBell::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_BELL));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_mem_aarch64_t>>(
                 "guest_mem", guest_mem_aarch64, ZxPacketGuestMemAArch64::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_MEM))
        ->DisplayIfArch(debug_ipc::Arch::kArm64);
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_mem_x86_t>>(
                 "guest_mem", guest_mem_x86, ZxPacketGuestMemX86::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_MEM))
        ->DisplayIfArch(debug_ipc::Arch::kX64);
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_io_t>>(
                 "guest_io", guest_io, ZxPacketGuestIo::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_IO));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_vcpu_t>>(
                 "guest_vcpu", guest_vcpu, ZxPacketGuestVcpu::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_VCPU));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_interrupt_t>>(
                 "interrupt", interrupt, ZxPacketInterrupt::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_INTERRUPT));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_page_request_t>>(
                 "page_request", page_request, ZxPacketPageRequest::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_PAGE_REQUEST));
  }
  ZxPortPacket(const ZxPortPacket&) = delete;
  ZxPortPacket& operator=(const ZxPortPacket&) = delete;
  static ZxPortPacket* instance_;
};

ZxPortPacket* ZxPortPacket::instance_ = nullptr;

const ZxPortPacket* ZxPortPacket::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPortPacket;
  }
  return instance_;
}

void SyscallDecoderDispatcher::Populate() {
  {
    Syscall* zx_clock_get = Add("zx_clock_get", SyscallReturnType::kStatus);
    // Arguments
    auto clock_id = zx_clock_get->Argument<zx_clock_t>(SyscallType::kClock);
    auto out = zx_clock_get->PointerArgument<zx_time_t>(SyscallType::kTime);
    // Inputs
    zx_clock_get->Input<zx_clock_t>("clock_id",
                                    std::make_unique<ArgumentAccess<zx_clock_t>>(clock_id));
    // Outputs
    zx_clock_get->Output<zx_time_t>(ZX_OK, "out", std::make_unique<ArgumentAccess<zx_time_t>>(out));
  }

  { Add("zx_clock_get_monotonic", SyscallReturnType::kTime); }

  {
    Syscall* zx_nanosleep = Add("zx_nanosleep", SyscallReturnType::kStatus);
    // Arguments
    auto deadline = zx_nanosleep->Argument<zx_time_t>(SyscallType::kTime);
    // Inputs
    zx_nanosleep->Input<zx_time_t>("deadline",
                                   std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
  }

  { Add("zx_ticks_get", SyscallReturnType::kTicks); }

  { Add("zx_ticks_per_second", SyscallReturnType::kTicks); }

  {
    Syscall* zx_deadline_after = Add("zx_deadline_after", SyscallReturnType::kTime);
    // Arguments
    auto nanoseconds = zx_deadline_after->Argument<zx_duration_t>(SyscallType::kDuration);
    // Inputs
    zx_deadline_after->Input<zx_duration_t>(
        "nanoseconds", std::make_unique<ArgumentAccess<zx_duration_t>>(nanoseconds));
  }

  {
    Syscall* zx_clock_adjust = Add("zx_clock_adjust", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_clock_adjust->Argument<zx_handle_t>(SyscallType::kHandle);
    auto clock_id = zx_clock_adjust->Argument<zx_clock_t>(SyscallType::kClock);
    auto offset = zx_clock_adjust->Argument<int64_t>(SyscallType::kInt64);
    // Inputs
    zx_clock_adjust->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_clock_adjust->Input<zx_clock_t>("clock_id",
                                       std::make_unique<ArgumentAccess<zx_clock_t>>(clock_id));
    zx_clock_adjust->Input<int64_t>("offset", std::make_unique<ArgumentAccess<int64_t>>(offset));
  }

  {
    Syscall* zx_channel_create = Add("zx_channel_create", SyscallReturnType::kStatus);
    // Arguments
    auto options = zx_channel_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out0 = zx_channel_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto out1 = zx_channel_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_channel_create->Input<uint32_t>("options",
                                       std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_channel_create->Output<zx_handle_t>(ZX_OK, "out0",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(out0));
    zx_channel_create->Output<zx_handle_t>(ZX_OK, "out1",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(out1));
  }

  {
    Syscall* zx_channel_read = Add("zx_channel_read", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_channel_read->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_channel_read->Argument<uint32_t>(SyscallType::kUint32);
    auto bytes = zx_channel_read->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto handles = zx_channel_read->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto num_bytes = zx_channel_read->Argument<uint32_t>(SyscallType::kUint32);
    auto num_handles = zx_channel_read->Argument<uint32_t>(SyscallType::kUint32);
    auto actual_bytes = zx_channel_read->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto actual_handles = zx_channel_read->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_channel_read->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_channel_read->Input<uint32_t>("options",
                                     std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_channel_read->Input<uint32_t>("num_bytes",
                                     std::make_unique<ArgumentAccess<uint32_t>>(num_bytes));
    zx_channel_read->Input<uint32_t>("num_handles",
                                     std::make_unique<ArgumentAccess<uint32_t>>(num_handles));
    // Outputs
    zx_channel_read->OutputFidlMessageHandle(
        ZX_OK, "", SyscallFidlType::kInputMessage,
        std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<ArgumentAccess<uint8_t>>(bytes),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes),
        std::make_unique<ArgumentAccess<zx_handle_t>>(handles),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
    zx_channel_read->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_bytes",
                                      std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes));
    zx_channel_read->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_handles",
                                      std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
  }

  {
    Syscall* zx_channel_read_etc = Add("zx_channel_read_etc", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_channel_read_etc->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_channel_read_etc->Argument<uint32_t>(SyscallType::kUint32);
    auto bytes = zx_channel_read_etc->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto handles = zx_channel_read_etc->PointerArgument<zx_handle_info_t>(SyscallType::kHandle);
    auto num_bytes = zx_channel_read_etc->Argument<uint32_t>(SyscallType::kUint32);
    auto num_handles = zx_channel_read_etc->Argument<uint32_t>(SyscallType::kUint32);
    auto actual_bytes = zx_channel_read_etc->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto actual_handles = zx_channel_read_etc->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_channel_read_etc->Input<zx_handle_t>("handle",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_channel_read_etc->Input<uint32_t>("options",
                                         std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_channel_read_etc->Input<uint32_t>("num_bytes",
                                         std::make_unique<ArgumentAccess<uint32_t>>(num_bytes));
    zx_channel_read_etc->Input<uint32_t>("num_handles",
                                         std::make_unique<ArgumentAccess<uint32_t>>(num_handles));
    // Outputs
    zx_channel_read_etc->OutputFidlMessageHandleInfo(
        ZX_OK, "", SyscallFidlType::kInputMessage,
        std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<ArgumentAccess<uint8_t>>(bytes),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes),
        std::make_unique<ArgumentAccess<zx_handle_info_t>>(handles),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
    zx_channel_read_etc->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_bytes",
                                          std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes));
    zx_channel_read_etc->Output<uint32_t>(
        ZX_ERR_BUFFER_TOO_SMALL, "actual_handles",
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
  }

  {
    Syscall* zx_channel_write = Add("zx_channel_write", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_channel_write->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_channel_write->Argument<uint32_t>(SyscallType::kUint32);
    auto bytes = zx_channel_write->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto num_bytes = zx_channel_write->Argument<uint32_t>(SyscallType::kUint32);
    auto handles = zx_channel_write->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto num_handles = zx_channel_write->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_channel_write->Input<zx_handle_t>("handle",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_channel_write->Input<uint32_t>("options",
                                      std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_channel_write->InputFidlMessage("", SyscallFidlType::kOutputMessage,
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
                                       std::make_unique<ArgumentAccess<uint8_t>>(bytes),
                                       std::make_unique<ArgumentAccess<uint32_t>>(num_bytes),
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handles),
                                       std::make_unique<ArgumentAccess<uint32_t>>(num_handles));
  }
  {
    Syscall* zx_channel_call = Add("zx_channel_call", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_channel_call->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_channel_call->Argument<uint32_t>(SyscallType::kUint32);
    auto deadline = zx_channel_call->Argument<zx_time_t>(SyscallType::kTime);
    auto args = zx_channel_call->PointerArgument<zx_channel_call_args_t>(SyscallType::kStruct);
    auto actual_bytes = zx_channel_call->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto actual_handles = zx_channel_call->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_channel_call->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_channel_call->Input<uint32_t>("options",
                                     std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_channel_call->Input<zx_time_t>("deadline",
                                      std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
    zx_channel_call->Input<uint32_t>(
        "rd_num_bytes", std::make_unique<FieldAccess<zx_channel_call_args, uint32_t>>(
                            args, ZxChannelCallArgs::rd_num_bytes, SyscallType::kUint32));
    zx_channel_call->Input<uint32_t>(
        "rd_num_handles", std::make_unique<FieldAccess<zx_channel_call_args, uint32_t>>(
                              args, ZxChannelCallArgs::rd_num_handles, SyscallType::kUint32));
    zx_channel_call->InputFidlMessage(
        "", SyscallFidlType::kOutputRequest, std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<PointerFieldAccess<zx_channel_call_args, uint8_t>>(
            args, ZxChannelCallArgs::wr_bytes, SyscallType::kUint8),
        std::make_unique<FieldAccess<zx_channel_call_args, uint32_t>>(
            args, ZxChannelCallArgs::wr_num_bytes, SyscallType::kUint32),
        std::make_unique<PointerFieldAccess<zx_channel_call_args, zx_handle_t>>(
            args, ZxChannelCallArgs::wr_handles, SyscallType::kHandle),
        std::make_unique<FieldAccess<zx_channel_call_args, uint32_t>>(
            args, ZxChannelCallArgs::wr_num_handles, SyscallType::kUint32));
    // Outputs
    zx_channel_call->OutputFidlMessageHandle(
        ZX_OK, "", SyscallFidlType::kInputResponse,
        std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<PointerFieldAccess<zx_channel_call_args, uint8_t>>(
            args, ZxChannelCallArgs::rd_bytes, SyscallType::kUint8),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes),
        std::make_unique<PointerFieldAccess<zx_channel_call_args, zx_handle_t>>(
            args, ZxChannelCallArgs::rd_handles, SyscallType::kHandle),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
    zx_channel_call->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_bytes",
                                      std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes));
    zx_channel_call->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_handles",
                                      std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
  }

  {
    Syscall* zx_port_create = Add("zx_port_create", SyscallReturnType::kStatus);
    // Arguments
    auto options = zx_port_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out = zx_port_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_port_create->Input<uint32_t>("options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_port_create->Output<zx_handle_t>(ZX_OK, "out",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(out));
  }

  {
    Syscall* zx_port_queue = Add("zx_port_queue", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_port_queue->Argument<zx_handle_t>(SyscallType::kHandle);
    auto packet = zx_port_queue->PointerArgument<zx_port_packet_t>(SyscallType::kStruct);
    // Inputs
    zx_port_queue->Input<zx_handle_t>("handle",
                                      std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_port_queue->InputObject<zx_port_packet_t>(
        "packet", std::make_unique<ArgumentAccess<zx_port_packet_t>>(packet),
        ZxPortPacket::GetClass());
  }

  {
    Syscall* zx_port_wait = Add("zx_port_wait", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_port_wait->Argument<zx_handle_t>(SyscallType::kHandle);
    auto deadline = zx_port_wait->Argument<zx_time_t>(SyscallType::kTime);
    auto packet = zx_port_wait->PointerArgument<zx_port_packet_t>(SyscallType::kStruct);
    // Inputs
    zx_port_wait->Input<zx_handle_t>("handle",
                                     std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_port_wait->Input<zx_time_t>("deadline",
                                   std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
    // Outputs
    zx_port_wait->OutputObject<zx_port_packet_t>(
        ZX_OK, "packet", std::make_unique<ArgumentAccess<zx_port_packet_t>>(packet),
        ZxPortPacket::GetClass());
  }

  {
    Syscall* zx_port_cancel = Add("zx_port_cancel", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_port_cancel->Argument<zx_handle_t>(SyscallType::kHandle);
    auto source = zx_port_cancel->Argument<zx_handle_t>(SyscallType::kHandle);
    auto key = zx_port_cancel->Argument<uint64_t>(SyscallType::kUint64);
    // Inputs
    zx_port_cancel->Input<zx_handle_t>("handle",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_port_cancel->Input<zx_handle_t>("source",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(source));
    zx_port_cancel->Input<uint64_t>("key", std::make_unique<ArgumentAccess<uint64_t>>(key));
  }
}

}  // namespace fidlcat
