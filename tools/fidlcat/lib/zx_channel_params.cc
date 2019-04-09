// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zx_channel_params.h"

#include <vector>

#include "src/developer/debug/ipc/register_desc.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/lib/fxl/logging.h"

namespace fidlcat {

namespace {

// Helper function to convert a vector of bytes to a T.
template <typename T>
T GetValueFromBytes(const std::vector<uint8_t>& bytes) {
  T ret = 0;
  for (uint64_t i = 0; i < sizeof(ret) && i < bytes.size(); i++) {
    ret |= ((uint64_t)(bytes[i])) << (i * 8);
  }
  return ret;
}

// Helper function to convert the value in a register to a T
template <typename T>
T GetRegisterValue(const std::vector<zxdb::Register>& regs,
                   const debug_ipc::RegisterID id) {
  for (const zxdb::Register& reg : regs) {
    if (reg.id() == id) {
      return GetValueFromBytes<T>(reg.data());
    }
  }
  return 0;
}

// Grovels through the |dump| and constructs a local copy of the bytes starting
// at |bytes_address| and continuing for |num_bytes|.
std::unique_ptr<uint8_t[]> MemoryDumpToBytes(uint64_t bytes_address,
                                             uint32_t num_bytes,
                                             const zxdb::MemoryDump& dump) {
  std::unique_ptr<uint8_t[]> output_buffer =
      std::make_unique<uint8_t[]>(num_bytes);
  // replace with memset, or poison pattern.
  for (uint32_t i = 0; i < num_bytes; i++) {
    output_buffer[i] = 0;
  }
  size_t output_offset = 0;

  for (const debug_ipc::MemoryBlock& block : dump.blocks()) {
    if (!block.valid) {
      continue;
    }
    size_t block_offset = 0;
    if (block.address < bytes_address) {
      if (block.address + block.size < bytes_address) {
        continue;
      }
      block_offset = bytes_address - block.address;
    }
    while (block_offset < block.size && output_offset < num_bytes) {
      output_buffer[output_offset] = block.data[block_offset];
      output_offset++;
      block_offset++;
    }
  }
  return output_buffer;
}

}  // namespace

void ZxChannelWriteParams::BuildZxChannelWriteParamsAndContinue(
    fxl::WeakPtr<zxdb::Thread> thread, const zxdb::RegisterSet& registers,
    ZxChannelWriteCallback&& fn) {
  if (registers.arch() == debug_ipc::Arch::kX64) {
    auto regs_it = registers.category_map().find(
        debug_ipc::RegisterCategory::Type::kGeneral);
    if (regs_it == registers.category_map().end()) {
      zxdb::Err err(zxdb::ErrType::kInput,
                    "zx_channel_write params not found?");
      ZxChannelWriteParams params;
      fn(err, params);
      return;
    }
    ZxChannelWriteParams::BuildX86AndContinue(thread, regs_it->second,
                                              std::move(fn));
    return;
  } else if (registers.arch() == debug_ipc::Arch::kArm64) {
    zxdb::Err err(zxdb::ErrType::kCanceled, "ARM64 not supported yet");
    ZxChannelWriteParams params;
    fn(err, params);
  } else {
    zxdb::Err err(zxdb::ErrType::kCanceled, "Unknown arch");
    ZxChannelWriteParams params;
    fn(err, params);
  }
  return;
}

// Assuming that |thread| is stopped in a zx_channel_write, and that |regs| is
// the set of registers for that thread, and that both are on a connected x64
// device, do what is necessary to populate |params| and pass them to |fn|.
//
// This remains pretty brittle WRT the order of paramaters to zx_channel_write
// and the x86 calling conventions.  Those things aren't likely to change, but
// if they did, we'd have to update this.
void ZxChannelWriteParams::BuildX86AndContinue(
    fxl::WeakPtr<zxdb::Thread> thread, const std::vector<zxdb::Register>& regs,
    ZxChannelWriteCallback&& fn) {
  // The order of parameters in the System V AMD64 ABI we use, according to
  // Wikipedia:
  zx_handle_t handle =
      GetRegisterValue<zx_handle_t>(regs, debug_ipc::RegisterID::kX64_rdi);
  uint32_t options =
      GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kX64_rsi);
  uint64_t bytes_address =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kX64_rdx);
  uint32_t num_bytes =
      GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kX64_rcx);
  uint64_t handles_address =
      GetRegisterValue<uint64_t>(regs, debug_ipc::RegisterID::kX64_r8);
  uint32_t num_handles =
      GetRegisterValue<uint32_t>(regs, debug_ipc::RegisterID::kX64_r9);

  // We'll do something with handles_address eventually.
  (void)handles_address;

  if (num_bytes != 0) {
    thread->GetProcess()->ReadMemory(
        bytes_address, num_bytes,
        [handle, options, bytes_address, num_bytes, handles_address,
         num_handles,
         fn = std::move(fn)](const zxdb::Err& err, zxdb::MemoryDump dump) {
          (void)handles_address;
          ZxChannelWriteParams params;
          if (err.ok()) {
            auto bytes = MemoryDumpToBytes(bytes_address, num_bytes, dump);
            zx_handle_t* handles = nullptr;
            params = ZxChannelWriteParams(handle, options, std::move(bytes),
                                          num_bytes, handles, num_handles);
            fn(err, params);
          } else {
            std::string msg = "Failed to build zx_channel_write params: ";
            msg.append(err.msg());
            zxdb::Err e = zxdb::Err(err.type(), msg);
            fn(e, params);
          }
        });
  } else {
    ZxChannelWriteParams params(handle, options, nullptr, num_bytes, nullptr,
                                num_handles);
    zxdb::Err err;
    fn(err, params);
  }
  return;
}

}  // namespace fidlcat
