// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zx_channel_params.h"

#include <vector>

#include "src/developer/debug/ipc/register_desc.h"
#include "src/developer/debug/shared/message_loop.h"
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

// Grovels through the |dump| and constructs a local copy of the bytes into an
// array of type |T|, starting at |bytes_address| and continuing for |count|
// elements.
template <typename T>
std::unique_ptr<T> MemoryDumpToArray(uint64_t bytes_address, uint32_t count,
                                     const zxdb::MemoryDump& dump) {
  static_assert(std::is_array<T>::value || std::is_pointer<T>::value,
                "MemoryDump can only be used for pointer types");
  std::unique_ptr<T> output_buffer = std::make_unique<T>(count);
  // replace with memset, or poison pattern.
  for (uint32_t i = 0; i < count; i++) {
    output_buffer[i] = 0;
  }
  uint8_t* buffer_as_bytes = reinterpret_cast<uint8_t*>(output_buffer.get());
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
    while (block_offset < block.size &&
           output_offset < (sizeof(output_buffer[0]) * count)) {
      buffer_as_bytes[output_offset] = block.data[block_offset];
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

  struct ParamStore {
    // The params, which will move from partially-constructed to fully
    // constructed.
    ZxChannelWriteParams params;

    // Any errs that are propagated from the memory reads.
    zxdb::Err err;

    // The last callback to run, which invokes the ZxChannelWriteCallback
    fit::function<void()> final_cb;
  };

  ParamStore* store = new ParamStore();
  ZxChannelWriteParams new_params(handle, options, nullptr, num_bytes, nullptr,
                                  num_handles);
  store->params = std::move(new_params);
  store->final_cb = [store, fn = std::move(fn)]() {
    if (!store->err.ok()) {
      ZxChannelWriteParams p;
      fn(store->err, p);
    } else {
      zxdb::Err err;
      fn(err, store->params);
    }
    delete store;
  };

  if (num_bytes != 0) {
    thread->GetProcess()->ReadMemory(
        bytes_address, num_bytes,
        [store, num_bytes, bytes_address](const zxdb::Err& err,
                                          zxdb::MemoryDump dump) {
          std::unique_ptr<uint8_t[]> bytes;
          auto handles = std::move(store->params.handles_);
          if (err.ok()) {
            bytes =
                MemoryDumpToArray<uint8_t[]>(bytes_address, num_bytes, dump);
          } else {
            std::string msg = "Failed to build zx_channel_write params: ";
            msg.append(err.msg());
            zxdb::Err new_err(err.type(), msg);
            store->err = new_err;
          }
          ZxChannelWriteParams new_params(
              store->params.GetHandle(), store->params.GetOptions(),
              std::move(bytes), num_bytes, std::move(handles),
              store->params.GetNumHandles());
          store->params = std::move(new_params);
          if (store->params.IsComplete() || !store->err.ok()) {
            store->final_cb();
          }
        });
  }

  if (num_handles != 0) {
    thread->GetProcess()->ReadMemory(
        handles_address, num_handles * sizeof(zx_handle_t),
        [store, num_handles, handles_address](const zxdb::Err& err,
                                              zxdb::MemoryDump dump) {
          std::unique_ptr<zx_handle_t[]> handles;
          auto bytes = std::move(store->params.bytes_);
          if (err.ok()) {
            handles = MemoryDumpToArray<zx_handle_t[]>(handles_address,
                                                       num_handles, dump);
          } else {
            std::string msg = "Failed to build zx_channel_write params: ";
            msg.append(err.msg());
            zxdb::Err new_err(err.type(), msg);
            store->err = new_err;
          }
          ZxChannelWriteParams new_params(
              store->params.GetHandle(), store->params.GetOptions(),
              std::move(bytes), store->params.GetNumBytes(), std::move(handles),
              store->params.GetNumHandles());
          store->params = std::move(new_params);
          if (store->params.IsComplete() || !store->err.ok()) {
            store->final_cb();
          }
        });
  }

  if (num_handles == 0 && num_bytes == 0) {
    store->final_cb();
  }
}

}  // namespace fidlcat
