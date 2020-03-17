// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_SESSION_H_
#define ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_SESSION_H_

#include <string.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <lib/async/cpp/wait.h>
#include <lib/trace-provider/handler.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>

#include "fnv1hash.h"

namespace trace {
namespace internal {

class Session final : public trace::TraceHandler {
 public:
  static void InitializeEngine(async_dispatcher_t* dispatcher,
                               trace_buffering_mode_t buffering_mode, zx::vmo buffer, zx::fifo fifo,
                               std::vector<std::string> categories);
  static void StartEngine(trace_start_mode_t start_mode);
  static void StopEngine();
  static void TerminateEngine();

  ~Session() override;

 private:
  Session(async_dispatcher_t* dispatcher, void* buffer, size_t buffer_num_bytes, zx::fifo fifo,
          std::vector<std::string> categories);

  // |trace::TraceHandler|
  bool IsCategoryEnabled(const char* category) override;
  void TraceStarted() override;
  void TraceStopped(zx_status_t disposition) override;
  void TraceTerminated() override;
  // This is called in streaming mode to notify the trace manager that
  // buffer |buffer_number| is full and needs to be saved.
  void NotifyBufferFull(uint32_t wrapped_count, uint64_t durable_data_end) override;
  void SendTrigger(const char* trigger_name) override;

  void HandleFifo(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                  const zx_packet_signal_t* signal);
  bool ReadFifoMessage();
  void SendFifoPacket(const trace_provider_packet_t* packet);

  // This is called in streaming mode when TraceManager reports back that
  // it has saved the buffer.
  static zx_status_t MarkBufferSaved(uint32_t wrapped_count, uint64_t durable_data_end);

  async_dispatcher_t* const dispatcher_;
  void* buffer_;
  size_t buffer_num_bytes_;
  zx::fifo fifo_;
  async::WaitMethod<Session, &Session::HandleFifo> fifo_wait_;
  std::vector<std::string> const categories_;

  using CString = const char*;

  // For faster implementation of IsCategoryEnabled().
  struct StringSetEntry {
    StringSetEntry() : string(nullptr) {}
    explicit StringSetEntry(const CString& string) : string(string) {}

    const CString string;

    std::size_t operator()(const StringSetEntry& key) const noexcept {
      return fnv1a64str(key.string);
    }

    bool operator()(const StringSetEntry& lhs, const StringSetEntry& rhs) const {
      return strcmp(lhs.string, rhs.string) == 0;
    }
  };

  using StringSet = std::unordered_set<StringSetEntry, StringSetEntry, StringSetEntry>;
  StringSet enabled_category_set_;

  Session(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(const Session&) = delete;
  Session& operator=(Session&&) = delete;
};

}  // namespace internal
}  // namespace trace

#endif  // ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_SESSION_H_
