// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/inference.h"

#include <zircon/processargs.h>
#include <zircon/types.h>

#include <ios>
#include <ostream>

#include "src/lib/fidl_codec/printer.h"
#include "tools/fidlcat/lib/syscall_decoder.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

void Inference::CreateHandleInfo(zx_koid_t thread_koid, zx_handle_t handle) {
  int64_t timestamp = time(NULL);
  Thread* thread = dispatcher_->SearchThread(thread_koid);
  FX_DCHECK(thread != nullptr);
  dispatcher_->CreateHandleInfo(thread, handle, timestamp, /*startup=*/false);
}

bool Inference::NeedsToLoadHandleInfo(zx_koid_t tid, zx_handle_t handle) const {
  Thread* thread = dispatcher_->SearchThread(tid);
  FX_DCHECK(thread != nullptr);
  HandleInfo* handle_info = thread->process()->SearchHandleInfo(handle);
  if (handle_info == nullptr) {
    int64_t timestamp = time(NULL);
    handle_info = dispatcher_->CreateHandleInfo(thread, handle, timestamp, /*startup=*/false);
  }
  return handle_info->koid() == ZX_KOID_INVALID;
}

// This is the first function which is intercepted. This gives us information about
// all the handles an application have at startup. However, for directory handles,
// we don't have the name of the directory.
void Inference::ExtractHandleInfos(SyscallDecoder* decoder) {
  constexpr int kNhandles = 0;
  constexpr int kHandles = 1;
  constexpr int kHandleInfo = 2;
  // Get the values which have been harvest by the debugger using they argument number.
  uint32_t nhandles = decoder->ArgumentValue(kNhandles);
  const zx_handle_t* handles =
      reinterpret_cast<const zx_handle_t*>(decoder->ArgumentContent(Stage::kEntry, kHandles));
  const uint32_t* handle_info =
      reinterpret_cast<const zx_handle_t*>(decoder->ArgumentContent(Stage::kEntry, kHandleInfo));
  int64_t timestamp = time(NULL);
  // Get the information about all the handles.
  // The meaning of handle info is described in zircon/system/public/zircon/processargs.h
  for (uint32_t handle = 0; handle < nhandles; ++handle) {
    if (handles[handle] != 0) {
      dispatcher_->CreateHandleInfo(decoder->fidlcat_thread(), handles[handle], timestamp,
                                    /*startup=*/true);
      uint32_t type = PA_HND_TYPE(handle_info[handle]);
      switch (type) {
        case PA_FD:
          AddInferredHandleInfo(decoder->fidlcat_thread()->process()->koid(), handles[handle], "fd",
                                PA_HND_ARG(handle_info[handle]), "");
          break;
        case PA_DIRECTORY_REQUEST:
          AddInferredHandleInfo(decoder->fidlcat_thread()->process()->koid(), handles[handle],
                                "directory-request", "/", "");
          break;
        default:
          AddInferredHandleInfo(decoder->fidlcat_thread()->process()->koid(), handles[handle],
                                type);
          break;
      }
    }
  }
}

// This is the second function which is intercepted. This gives us information about
// all the handles which have not been used by processargs_extract_handles.
// This only adds information about directories.
void Inference::LibcExtensionsInit(SyscallDecoder* decoder) {
  constexpr int kHandleCount = 0;
  constexpr int kHandles = 1;
  constexpr int kHandleInfo = 2;
  constexpr int kNameCount = 3;
  constexpr int kNames = 4;
  // Get the values which have been harvest by the debugger using they argument number.
  uint32_t handle_count = decoder->ArgumentValue(kHandleCount);
  const zx_handle_t* handles =
      reinterpret_cast<const zx_handle_t*>(decoder->ArgumentContent(Stage::kEntry, kHandles));
  const uint32_t* handle_info =
      reinterpret_cast<const zx_handle_t*>(decoder->ArgumentContent(Stage::kEntry, kHandleInfo));
  uint32_t name_count = decoder->ArgumentValue(kNameCount);
  const uint64_t* names =
      reinterpret_cast<const uint64_t*>(decoder->ArgumentContent(Stage::kEntry, kNames));
  int64_t timestamp = time(NULL);
  // Get the information about the remaining handles.
  // The meaning of handle info is described in zircon/system/public/zircon/processargs.h
  for (uint32_t handle = 0; handle < handle_count; ++handle) {
    if (handles[handle] != 0) {
      dispatcher_->CreateHandleInfo(decoder->fidlcat_thread(), handles[handle], timestamp,
                                    /*startup=*/true);
      uint32_t type = PA_HND_TYPE(handle_info[handle]);
      switch (type) {
        case PA_NS_DIR: {
          uint32_t index = PA_HND_ARG(handle_info[handle]);
          AddInferredHandleInfo(
              decoder->fidlcat_thread()->process()->koid(), handles[handle], "dir",
              (index < name_count) ? reinterpret_cast<const char*>(
                                         decoder->BufferContent(Stage::kEntry, names[index]))
                                   : "",
              "");
          break;
        }
        case PA_FD:
          AddInferredHandleInfo(decoder->fidlcat_thread()->process()->koid(), handles[handle], "fd",
                                PA_HND_ARG(handle_info[handle]), "");
          break;
        case PA_DIRECTORY_REQUEST:
          AddInferredHandleInfo(decoder->fidlcat_thread()->process()->koid(), handles[handle],
                                "directory-request", "/", "");
          break;
        default:
          AddInferredHandleInfo(decoder->fidlcat_thread()->process()->koid(), handles[handle],
                                type);
          break;
      }
    }
  }
}

void Inference::InferMessage(const OutputEvent* event,
                             const fidl_codec::semantic::MethodSemantic* semantic,
                             fidl_codec::semantic::ContextType context_type) {
  if (semantic == nullptr) {
    return;
  }
  const fidl_codec::HandleValue* handle_value = event->invoked_event()->GetHandleValue(
      event->syscall()->SearchInlineMember("handle", /*invoked=*/true));
  if (handle_value->handle().handle != ZX_HANDLE_INVALID) {
    const fidl_codec::FidlMessageValue* sent = event->invoked_event()->GetMessage();
    const fidl_codec::FidlMessageValue* received = event->GetMessage();
    const fidl_codec::StructValue* request = nullptr;
    const fidl_codec::StructValue* response = nullptr;
    switch (context_type) {
      case fidl_codec::semantic::ContextType::kRead:
        if (received != nullptr) {
          request = received->decoded_request();
          response = received->decoded_response();
        }
        break;
      case fidl_codec::semantic::ContextType::kWrite:
        if (sent != nullptr) {
          request = sent->decoded_request();
          response = sent->decoded_response();
        }
        break;
      case fidl_codec::semantic::ContextType::kCall:
        if (sent != nullptr) {
          request = sent->decoded_request();
          response = received->decoded_response();
        }
        break;
    }
    fidl_codec::semantic::AssignmentSemanticContext context(
        this, event->thread()->process()->koid(), event->thread()->koid(),
        handle_value->handle().handle, context_type, request, response);
    semantic->ExecuteAssignments(&context);
  }
}

void Inference::ZxChannelCreate(const OutputEvent* event) {
  const fidl_codec::HandleValue* out0 =
      event->GetHandleValue(event->syscall()->SearchInlineMember("out0", /*invoked=*/false));
  FX_DCHECK(out0 != nullptr);
  const fidl_codec::HandleValue* out1 =
      event->GetHandleValue(event->syscall()->SearchInlineMember("out1", /*invoked=*/false));
  FX_DCHECK(out1 != nullptr);
  if ((out0->handle().handle != ZX_HANDLE_INVALID) &&
      (out1->handle().handle != ZX_HANDLE_INVALID)) {
    int64_t timestamp = time(NULL);
    dispatcher_->CreateHandleInfo(event->thread(), out0->handle().handle, timestamp,
                                  /*startup=*/false);
    dispatcher_->CreateHandleInfo(event->thread(), out1->handle().handle, timestamp,
                                  /*startup=*/false);
    // Provides the minimal semantic for both handles (that is they are channels).
    AddInferredHandleInfo(event->thread()->process()->koid(), out0->handle().handle, "channel",
                          next_channel_++, "");
    AddInferredHandleInfo(event->thread()->process()->koid(), out1->handle().handle, "channel",
                          next_channel_++, "");
    // Links the two channels.
    AddLinkedHandles(event->thread()->process()->koid(), out0->handle().handle,
                     out1->handle().handle);
  }
}

void Inference::ZxPortCreate(const OutputEvent* event) {
  const fidl_codec::HandleValue* out =
      event->GetHandleValue(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
  FX_DCHECK(out != nullptr);
  if (out->handle().handle != ZX_HANDLE_INVALID) {
    int64_t timestamp = time(NULL);
    dispatcher_->CreateHandleInfo(event->thread(), out->handle().handle, timestamp,
                                  /*startup=*/false);
    // Provides the minimal semantic for the handle (that is it's a port).
    AddInferredHandleInfo(event->thread()->process()->koid(), out->handle().handle, "port",
                          next_port_++, "");
  }
}

void Inference::ZxTimerCreate(const OutputEvent* event) {
  const fidl_codec::HandleValue* out =
      event->GetHandleValue(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
  FX_DCHECK(out != nullptr);
  if (out->handle().handle != ZX_HANDLE_INVALID) {
    int64_t timestamp = time(NULL);
    dispatcher_->CreateHandleInfo(event->thread(), out->handle().handle, timestamp,
                                  /*startup=*/false);
    // Provides the minimal semantic for the handle (that is it's a timer).
    AddInferredHandleInfo(event->thread()->process()->koid(), out->handle().handle, "timer",
                          next_timer_++, "");
  }
}

}  // namespace fidlcat
