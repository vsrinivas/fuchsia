// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_CONTEXT_H_

#include <cstdint>
#include <utility>

#include <dap/protocol.h>
#include <dap/session.h>

#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process_observer.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/session_observer.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Session;
class Breakpoint;

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
class DebugAdapterContext : public ThreadObserver, ProcessObserver, SessionObserver {
 public:
  using DestroyConnectionCallback = std::function<void()>;

  explicit DebugAdapterContext(Session* session, debug::StreamBuffer* stream);
  virtual ~DebugAdapterContext();

  Session* session() { return session_; }
  dap::Session& dap() { return *dap_; }
  bool supports_run_in_terminal() { return supports_run_in_terminal_; }

  // Notification about the stream.
  void OnStreamReadable();

  // Callback to delete the connection and hence this context. This callback will be posted on
  // message loop.
  void set_destroy_connection_callback(DestroyConnectionCallback cb) {
    destroy_connection_cb_ = std::move(cb);
  }

  // SessionObserver implementation:
  void DidConnect(const Err& err) override;

  // ThreadObserver implementation:
  void DidCreateThread(Thread* thread) override;
  void WillDestroyThread(Thread* thread) override;
  void OnThreadStopped(Thread* thread, const StopInfo& info) override;
  void OnThreadFramesInvalidated(Thread* thread) override;

  // ProcessObserver implementation:
  void DidCreateProcess(Process* process, uint64_t timestamp) override;
  void WillDestroyProcess(Process* process, DestroyReason reason, int exit_code,
                          uint64_t timestamp) override;

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

  // Helper methods to get/set breakpoint to source file mapping.
  void StoreBreakpointForSource(const std::string& source, Breakpoint* bp);
  std::vector<fxl::WeakPtr<Breakpoint>>* GetBreakpointsForSource(const std::string& source);

  // TODO(fxbug.dev/69392): These 2 method deletes all breakpoints added by the debug adapter.
  // Breakpoints added from console are not deleted.
  void DeleteBreakpointsForSource(const std::string& source);
  void DeleteAllBreakpoints();

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

  DestroyConnectionCallback destroy_connection_cb_;

  // This is used when the DAP initialize request comes when the debugger has a pending connection
  // to the device. In this case, we want to defer the DAP initialze response until the connection
  // is resolved.
  fit::callback<void(dap::ResponseOrError<dap::InitializeResponse>)> send_initialize_response_;

  // This mapping is temporarily added to store all breakpoints added by debug adapter client. Once
  // http://fxbug.dev/69392 is fixed, this can removed in favor of using System::GetBreakpoints API
  // i.e. with breakpoint event, debug adapter client can be made aware of additional breakpoints
  // (from say zxdb console) and hence breakpoint list maintained by system will be identical to
  // this map in terms of the entries. One could traverse the entire system breakpoint list to get
  // breakpoints related to a source file instead of having to maintain a separate map.
  std::map<std::string, std::vector<fxl::WeakPtr<Breakpoint>>> source_to_bp_;

  void Init();
};

class DebugAdapterReader : public dap::Reader {
 public:
  explicit DebugAdapterReader(debug::StreamBuffer* stream) : stream_(stream) {}
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
  debug::StreamBuffer* stream_ = nullptr;
};

class DebugAdapterWriter : public dap::Writer {
 public:
  explicit DebugAdapterWriter(debug::StreamBuffer* stream) : stream_(stream) {}
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
  debug::StreamBuffer* stream_ = nullptr;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_DEBUG_ADAPTER_CONTEXT_H_
