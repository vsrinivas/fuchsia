// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_CONTEXT_H_

#include <cstdint>

#include <dap/protocol.h>
#include <dap/session.h>

#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process_observer.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Session;
class DebugAdapterServer;
class DebugAdapterReader;
class DebugAdapterWriter;

// Types of variables reported in variables request.
enum class VariablesType {
  kLocal = 0,
  kArguments,
  kRegister,
  kChildVariable,
  kVariablesTypeCount,  // Keep this in the end always
};

struct VariablesRecord {
  int64_t frame_id;
  VariablesType type = VariablesType::kVariablesTypeCount;
  // Fields to store children information corresponding to the record so that subsequent variables
  // request can be processed. Store the format node in `parent` if children exist. If `parent`'s
  // child has children, store a weak pointer to it in `child`.
  std::unique_ptr<FormatNode> parent;
  fxl::WeakPtr<FormatNode> child;
};

// Handles processing requests from debug adapter client with help from zxdb client session and dap
// library.
// Note: All methods in this class need to be executed on main thread to avoid concurrency bugs.
class DebugAdapterContext : public ThreadObserver, ProcessObserver {
 public:
  explicit DebugAdapterContext(Session* session, debug_ipc::StreamBuffer* stream);

  virtual ~DebugAdapterContext();

  Session* session() { return session_; }
  dap::Session& dap() { return *dap_; }
  bool supports_run_in_terminal() { return supports_run_in_terminal_; };

  // Notification about the stream.
  void OnStreamReadable();

  // ThreadObserver implementation:
  void DidCreateThread(Thread* thread) override;
  void WillDestroyThread(Thread* thread) override;
  void OnThreadStopped(Thread* thread, const StopInfo& info) override;
  void OnThreadFramesInvalidated(Thread* thread) override;

  // ProcessObserver implementation:
  void DidCreateProcess(Process* process, bool autoattached_to_new_process,
                        uint64_t timestamp) override;

  Target* GetCurrentTarget();
  Process* GetCurrentProcess();
  Thread* GetThread(uint64_t koid);

  // Checks if thread is in stopped state; returns error if not stopped.
  // `thread` can be nullptr, in which case an error is returned.
  Err CheckStoppedThread(Thread* thread);

  // Helper methods to get/set frame to ID mapping
  int64_t IdForFrame(Frame* frame, int stack_index);
  Frame* FrameforId(int64_t id);
  void DeleteFrameIdsForThread(Thread* thread);

  // Helper methods to get/set variables references
  int64_t IdForVariables(int64_t frame_id, VariablesType type,
                         std::unique_ptr<FormatNode> parent = nullptr,
                         fxl::WeakPtr<FormatNode> child = nullptr);
  VariablesRecord* VariablesRecordForID(int64_t id);
  void DeleteVariablesIdsForFrameId(int64_t id);

 private:
  Session* const session_;
  const std::unique_ptr<dap::Session> dap_;
  std::shared_ptr<DebugAdapterReader> reader_;
  std::shared_ptr<DebugAdapterWriter> writer_;

  bool supports_run_in_terminal_ = false;
  bool supports_invalidate_event_ = false;
  bool init_done_ = false;

  struct FrameRecord {
    uint64_t thread_koid = 0;
    int stack_index = 0;
  };
  std::map<int64_t, FrameRecord> id_to_frame_;
  int64_t next_frame_id_ = 1;

  std::map<int64_t, VariablesRecord> id_to_variables_;
  int64_t next_variables_id_ = 1;

  void Init();
};

class DebugAdapterReader : public dap::Reader {
 public:
  explicit DebugAdapterReader(debug_ipc::StreamBuffer* stream) : stream_(stream) {}
  size_t read(void* buffer, size_t n) override {
    if (!stream_) {
      return 0;
    }
    auto ret = stream_->Read(static_cast<char*>(buffer), n);
    return ret;
  }
  bool isOpen() override { return !!stream_; }

  void close() override { stream_ = nullptr; }

 private:
  debug_ipc::StreamBuffer* stream_ = nullptr;
};

class DebugAdapterWriter : public dap::Writer {
 public:
  explicit DebugAdapterWriter(debug_ipc::StreamBuffer* stream) : stream_(stream) {}
  bool write(const void* buffer, size_t n) override {
    if (!stream_) {
      return false;
    }
    stream_->Write(
        std::vector<char>(static_cast<const char*>(buffer), static_cast<const char*>(buffer) + n));
    return true;
  }
  bool isOpen() override { return !!stream_; }

  void close() override { stream_ = nullptr; }

 private:
  debug_ipc::StreamBuffer* stream_ = nullptr;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_CONTEXT_H_
