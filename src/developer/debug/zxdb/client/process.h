// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_H_

#include <stdint.h>

#include <map>
#include <memory>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/client/process_observer.h"
#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/lib/containers/cpp/circular_deque.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/observer_list.h"

namespace debug_ipc {
struct MemoryBlock;
struct Module;
struct ThreadRecord;
struct AddressRegion;
}  // namespace debug_ipc

namespace zxdb {

class Err;
struct InputLocation;
class BacktraceCache;
class MemoryDump;
class ProcessSymbols;
class Target;
class Thread;

class Process : public ClientObject {
 public:
  struct TLSHelpers {
    std::vector<uint8_t> thrd_t;
    std::vector<uint8_t> link_map_tls_modid;
    std::vector<uint8_t> tlsbase;
  };

  using GetTLSHelpersCallback = fit::callback<void(ErrOr<const TLSHelpers*>)>;

  // Documents how this process was started.
  // This is useful for user feedback.
  enum class StartType {
    kAttach,
    kComponent,
    kLaunch,
  };
  const char* StartTypeToString(StartType);

  Process(Session* session, StartType);
  ~Process() override;

  fxl::WeakPtr<Process> GetWeakPtr();

  // Returns the target associated with this process. Guaranteed non-null.
  virtual Target* GetTarget() const = 0;

  // The Process koid is guaranteed non-null.
  virtual uint64_t GetKoid() const = 0;

  // Returns the "name" of the process. This is the process object name which is normally based on
  // the file name, but isn't the same as the file name.
  virtual const std::string& GetName() const = 0;

  // Returns the interface for querying symbols for this process.
  virtual ProcessSymbols* GetSymbols() = 0;

  // Queries the process for the currently-loaded modules (this always
  // recomputes the list).
  virtual void GetModules(fit::callback<void(const Err&, std::vector<debug_ipc::Module>)>) = 0;

  // Queries the process for its address map if |address| is zero the entire map is requested. If
  // |address| is non-zero only the containing region if exists will be retrieved.
  virtual void GetAspace(
      uint64_t address,
      fit::callback<void(const Err&, std::vector<debug_ipc::AddressRegion>)>) const = 0;

  // Returns all threads in the process. This is as of the last update from the system. If the
  // program is currently running, the actual threads may be different since it can be
  // asynchronously creating and destroying them.
  //
  // Some programs also change thread names dynamically, so the names may be stale. Call
  // SyncThreads() to update the thread list with the debuggee.
  //
  // The pointers will only be valid until you return to the message loop.
  virtual std::vector<Thread*> GetThreads() const = 0;

  // Returns the thread in this process associated with the given koid.
  virtual Thread* GetThreadFromKoid(uint64_t koid) = 0;

  // Asynchronously refreshes the thread list from the debugged process. This will ensure the thread
  // names are up-to-date, and is also used after attaching when there are no thread notifications
  // for existing threads.
  //
  // If the Process is destroyed before the call completes, the callback will not be issued. If this
  // poses a problem in the future, we can add an error code to the callback, but will need to be
  // careful to make clear the Process object is not valid at that point (callers may want to use it
  // to format error messages).
  //
  // To get the computed threads, call GetThreads() once the callback runs.
  virtual void SyncThreads(fit::callback<void()> callback) = 0;

  // Pauses (suspends in Zircon terms) all threads in the process, it does not affect other
  // processes.
  //
  // The backend will try to ensure the threads are actually paused before issuing the on_paused
  // callback. But this is best effort and not guaranteed: both because there's a timeout for the
  // synchronous suspending and because a different continue message could race with the reply.
  virtual void Pause(fit::callback<void()> on_paused) = 0;

  // Applies to all threads in the process.
  // See Thread::Continue() for more detail on the forwarding of exceptions.
  virtual void Continue(bool forward_exceptions) = 0;

  // The callback does NOT mean the step has completed, but rather the setup for the function was
  // successful. Symbols and breakpoint setup can cause asynchronous failures.
  virtual void ContinueUntil(std::vector<InputLocation> locations,
                             fit::callback<void(const Err&)> cb) = 0;

  // Returns the SymbolDataProvider that can be used to evaluate symbols in the context of this
  // process. This will not have any frame information so the available operations will be limited.
  //
  // If the caller has a Frame, prefer Frame::GetSymbolDataProvider() which does have access to
  // registers and other frame data.
  virtual fxl::RefPtr<SymbolDataProvider> GetSymbolDataProvider() const = 0;

  // Get the TLS helper code for this process. These are memory blobs containing DWARF programs
  // which we can run to evaluate thread-local addresses. The callback is issued synchronously if
  // the data is available.
  virtual void GetTLSHelpers(GetTLSHelpersCallback cb) = 0;

  // Reads memory from the debugged process.
  virtual void ReadMemory(uint64_t address, uint32_t size,
                          fit::callback<void(const Err&, MemoryDump)> callback) = 0;

  // Write memory to the debugged process.
  virtual void WriteMemory(uint64_t address, std::vector<uint8_t> data,
                           fit::callback<void(const Err&)> callback) = 0;

  // Executes zx_object_get_info with ZX_INFO_HANDLE_TABLE for the process and gives the result
  // back.
  virtual void LoadInfoHandleTable(
      fit::callback<void(ErrOr<std::vector<debug_ipc::InfoHandleExtended>> handles)> callback) = 0;

  StartType start_type() const { return start_type_; }

  static constexpr size_t kMaxIOBufferSize = 1 * 1024 * 1024;  // In bytes.
  const containers::circular_deque<uint8_t>& get_stdout() const { return stdout_; }

  const containers::circular_deque<uint8_t>& get_stderr() const { return stderr_; }

 protected:
  containers::circular_deque<uint8_t> stdout_;
  containers::circular_deque<uint8_t> stderr_;

 private:
  StartType start_type_;

  fxl::WeakPtrFactory<Process> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Process);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PROCESS_H_
