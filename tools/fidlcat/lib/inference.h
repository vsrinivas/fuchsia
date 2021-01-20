// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_INFERENCE_H_
#define TOOLS_FIDLCAT_LIB_INFERENCE_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "src/lib/fidl_codec/semantic.h"

namespace fidlcat {

class HandleInfo;
class OutputEvent;
class SyscallDecoder;
class SyscallDecoderDispatcher;

// Object which hold the information we have about handles.
class Inference : public fidl_codec::semantic::HandleSemantic {
 public:
  explicit Inference(SyscallDecoderDispatcher* dispatcher) : dispatcher_(dispatcher) {}

  void AddKoidHandleInfo(zx_koid_t koid, HandleInfo* handle_info) {
    koid_handle_infos_[koid].emplace(handle_info);
  }

  const std::set<HandleInfo*>* GetKoidHandleInfos(zx_koid_t koid) const {
    auto result = koid_handle_infos_.find(koid);
    if (result == koid_handle_infos_.end()) {
      return nullptr;
    }
    return &result->second;
  }

  void CreateHandleInfo(int64_t timestamp, zx_koid_t thread_koid, zx_handle_t handle) override;

  bool NeedsToLoadHandleInfo(int64_t timestamp, zx_koid_t tid, zx_handle_t handle) const override;

  // Function called when processargs_extract_handles (from libc) is intercepted.
  void ExtractHandleInfos(int64_t timestamp, SyscallDecoder* decoder);

  // Function called when __libc_extensions_init (from libc) is intercepted.
  void LibcExtensionsInit(int64_t timestamp, SyscallDecoder* decoder);

  // Function call for channel functions which read/write FILD messages to try to infer some
  // semantic.
  void InferMessage(const OutputEvent* event, const fidl_codec::semantic::MethodSemantic* semantic,
                    fidl_codec::semantic::ContextType context_type);

  // Called after a zx_channel_create syscall has been displayed.
  void ZxChannelCreate(const OutputEvent* event);

  // Called after a zx_port_create syscall has been displayed.
  void ZxPortCreate(const OutputEvent* event);

  // Called after a zx_timer_create syscall has been displayed.
  void ZxTimerCreate(const OutputEvent* event);

 private:
  // The dispatcher which owns the inference.
  SyscallDecoderDispatcher* const dispatcher_;
  // Id for the next created channel.
  uint32_t next_channel_ = 0;
  // Id for the next created port.
  uint32_t next_port_ = 0;
  // Id for the next created timer.
  uint32_t next_timer_ = 0;
  // All the handle info associated with a koid. A handle can be transfered from one process to
  // another. Each time it keeps the same koid but this generates a different handle info.
  std::map<zx_koid_t, std::set<HandleInfo*>> koid_handle_infos_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_INFERENCE_H_
