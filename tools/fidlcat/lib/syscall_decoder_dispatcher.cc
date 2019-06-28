// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

#include <zircon/system/public/zircon/errors.h>
#include <zircon/system/public/zircon/types.h>

#include <cstdint>
#include <memory>

#include "tools/fidlcat/lib/syscall_decoder.h"

namespace fidlcat {

void SyscallFidlMessage::DisplayOutline(SyscallDisplayDispatcher* dispatcher,
                                        SyscallDecoder* decoder, int tabs,
                                        bool read, std::ostream& os) const {
  zx_handle_t handle = handle_->Value(decoder);
  const uint8_t* bytes = bytes_->Content(decoder);
  uint32_t num_bytes = num_bytes_->Value(decoder);
  const zx_handle_t* handles = handles_->Content(decoder);
  uint32_t num_handles = num_handles_->Value(decoder);
  if (!dispatcher->message_decoder_dispatcher().DecodeMessage(
          ULLONG_MAX, handle, bytes, num_bytes, handles, num_handles,
          /*read=*/read, os, tabs)) {
    os << "  " << dispatcher->colors().red << "Can't decode message"
       << dispatcher->colors().reset << " num_bytes=" << num_bytes
       << " num_handles=" << num_handles;
    if ((bytes != nullptr) && (num_bytes >= sizeof(fidl_message_header_t))) {
      const fidl_message_header_t* header =
          reinterpret_cast<const fidl_message_header_t*>(bytes);
      os << " ordinal=" << header->ordinal;
    }
    os << '\n';
  }
}

void SyscallDecoderDispatcher::DecodeSyscall(
    InterceptingThreadObserver* thread_observer, zxdb::Thread* thread,
    Syscall* syscall) {
  uint64_t thread_id = thread->GetKoid();
  auto current = syscall_decoders_.find(thread_id);
  if (current != syscall_decoders_.end()) {
    FXL_LOG(INFO) << "internal error: already decoding thread " << thread_id;
    return;
  }
  auto decoder = CreateDecoder(thread_observer, thread, thread_id, syscall);
  auto tmp = decoder.get();
  syscall_decoders_[thread_id] = std::move(decoder);
  tmp->Decode();
}

void SyscallDecoderDispatcher::DeleteDecoder(SyscallDecoder* decoder) {
  decoder->thread()->Continue();
  syscall_decoders_.erase(decoder->thread_id());
}

void SyscallDecoderDispatcher::Populate() {
  {
    Syscall* zx_channel_write = Add("zx_channel_write");
    // Arguments
    auto handle = zx_channel_write->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_channel_write->Argument<uint32_t>(SyscallType::kUint32);
    auto bytes =
        zx_channel_write->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto num_bytes = zx_channel_write->Argument<uint32_t>(SyscallType::kUint32);
    auto handles =
        zx_channel_write->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto num_handles =
        zx_channel_write->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_channel_write->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_channel_write->Input<uint32_t>(
        "options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_channel_write->InputFidlMessage(
        "", std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<ArgumentAccess<uint8_t>>(bytes),
        std::make_unique<ArgumentAccess<uint32_t>>(num_bytes),
        std::make_unique<ArgumentAccess<zx_handle_t>>(handles),
        std::make_unique<ArgumentAccess<uint32_t>>(num_handles));
  }

  {
    Syscall* zx_channel_read = Add("zx_channel_read");
    // Arguments
    auto handle = zx_channel_read->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_channel_read->Argument<uint32_t>(SyscallType::kUint32);
    auto bytes = zx_channel_read->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto handles =
        zx_channel_read->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto num_bytes = zx_channel_read->Argument<uint32_t>(SyscallType::kUint32);
    auto num_handles =
        zx_channel_read->Argument<uint32_t>(SyscallType::kUint32);
    auto actual_bytes =
        zx_channel_read->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto actual_handles =
        zx_channel_read->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_channel_read->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_channel_read->Input<uint32_t>(
        "options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_channel_read->Input<uint32_t>(
        "num_bytes", std::make_unique<ArgumentAccess<uint32_t>>(num_bytes));
    zx_channel_read->Input<uint32_t>(
        "num_handles", std::make_unique<ArgumentAccess<uint32_t>>(num_handles));
    // Outputs
    zx_channel_read->OutputFidlMessage(
        ZX_OK, "", std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<ArgumentAccess<uint8_t>>(bytes),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes),
        std::make_unique<ArgumentAccess<zx_handle_t>>(handles),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
    zx_channel_read->Output<uint32_t>(
        ZX_ERR_BUFFER_TOO_SMALL, "actual_bytes",
        std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes));
    zx_channel_read->Output<uint32_t>(
        ZX_ERR_BUFFER_TOO_SMALL, "actual_handles",
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
  }
}

std::unique_ptr<SyscallDecoder> SyscallDisplayDispatcher::CreateDecoder(
    InterceptingThreadObserver* thread_observer, zxdb::Thread* thread,
    uint64_t thread_id, const Syscall* syscall) {
  return std::make_unique<SyscallDecoder>(
      this, thread_observer, thread, thread_id, syscall,
      std::make_unique<SyscallDisplay>(this, os_));
}

}  // namespace fidlcat
