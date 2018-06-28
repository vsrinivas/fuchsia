// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <map>
#include <memory>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/bin/zxdb/client/process_observer.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"
#include "garnet/public/lib/fxl/observer_list.h"

namespace debug_ipc {
struct MemoryBlock;
struct Module;
struct ThreadRecord;
struct AddressRegion;
}

namespace zxdb {

class Err;
struct InputLocation;
class MemoryDump;
class ProcessSymbols;
class Target;
class Thread;

class Process : public ClientObject {
 public:
  Process(Session* session);
  ~Process() override;

  void AddObserver(ProcessObserver* observer);
  void RemoveObserver(ProcessObserver* observer);

  fxl::WeakPtr<Process> GetWeakPtr();

  // Returns the target associated with this process. Guaranteed non-null.
  virtual Target* GetTarget() const = 0;

  // The Process koid is guaranteed non-null.
  virtual uint64_t GetKoid() const = 0;

  // Returns the "name" of the process. This is the process object name which
  // is normally based on the file name, but isn't the same as the file name.
  virtual const std::string& GetName() const = 0;

  // Returns the interface for querying symbols for this process.
  virtual ProcessSymbols* GetSymbols() = 0;

  // Queries the process for the currently-loaded modules (this always
  // recomputes the list).
  virtual void GetModules(
      std::function<void(const Err&, std::vector<debug_ipc::Module>)>) = 0;

  // Queries the process for its address map if |address| is zero the entire
  // map is requested. If |address| is non-zero only the containing region
  // if exists will be retrieved.
  virtual void GetAspace(
      uint64_t address,
      std::function<void(const Err&, std::vector<debug_ipc::AddressRegion>)>)
      const = 0;

  // Returns all threads in the process. This is as of the last update from
  // the system. If the program is currently running, the actual threads may be
  // different since it can be asynchonously creating and destroying them.
  //
  // Some programs also change thread names dynamically, so the names may be
  // stale. Call SyncThreads() to update the thread list with the debuggee.
  //
  // The pointers will only be valid until you return to the message loop.
  virtual std::vector<Thread*> GetThreads() const = 0;

  // Returns the thread in this process associated with the given koid.
  virtual Thread* GetThreadFromKoid(uint64_t koid) = 0;

  // Asynchronously refreshes the thread list from the debugged process. This
  // will ensure the thread names are up-to-date, and is also used after
  // attaching when there are no thread notifications for existing threads.
  //
  // If the Process is destroyed before the call completes, the callback will
  // not be issued. If this poses a problem in the future, we can add an
  // error code to the callback, but will need to be careful to make clear the
  // Process object is not valid at that point (callers may want to use it to
  // format error messages).
  //
  // To get the computed threads, call GetThreads() once the callback runs.
  virtual void SyncThreads(std::function<void()> callback) = 0;

  // Applies to all threads in the process.
  virtual void Pause() = 0;
  virtual void Continue() = 0;

  // The callback does NOT mean the step has completed, but rather the setup
  // for the function was successful. Symbols and breakpoint setup can cause
  // asynchronous failures.
  virtual void ContinueUntil(const InputLocation& location,
                             std::function<void(const Err&)> cb) = 0;

  // Reads memory from the debugged process.
  virtual void ReadMemory(
      uint64_t address, uint32_t size,
      std::function<void(const Err&, MemoryDump)> callback) = 0;

 protected:
  fxl::ObserverList<ProcessObserver>& observers() { return observers_; }

 private:
  fxl::ObserverList<ProcessObserver> observers_;
  fxl::WeakPtrFactory<Process> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Process);
};

}  // namespace zxdb
