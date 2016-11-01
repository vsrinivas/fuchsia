// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/handle.h>
#include <mojo/system/message_pipe.h>
#include <mojo/system/result.h>
#include <mojo/system/time.h>
#include <mojo/system/wait.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "mojo/public/platform/dart/dart_handle_watcher.h"

#include "dart/runtime/include/dart_api.h"
#include "dart/runtime/include/dart_native_api.h"

namespace mojo {
namespace dart {

#define CONTROL_HANDLE_INDEX 0

static void PostNull(Dart_Port port) {
  if (port == ILLEGAL_PORT) {
    return;
  }
  Dart_CObject message;
  message.type = Dart_CObject_kNull;
  Dart_PostCObject(port, &message);
}

static void PostSignal(Dart_Port port, int32_t signalled) {
  if (port == ILLEGAL_PORT) {
    return;
  }
  Dart_PostInteger(port, signalled);
}

// The internal state of the handle watcher thread.
class HandleWatcherThreadState {
 public:
  HandleWatcherThreadState(MojoHandle control_pipe_consumer_handle);

  ~HandleWatcherThreadState();

  void Run();

 private:
  struct HandleWatcherTimer {
    int64_t deadline;
    Dart_Port port;

    // Sort on deadline.
    friend bool operator<(const HandleWatcherTimer& l,
                          const HandleWatcherTimer& r) {
      return l.deadline < r.deadline;
    }
  };

  void AddHandle(MojoHandle handle,
                 MojoHandleSignals signals,
                 Dart_Port port);

  void RemoveHandle(MojoHandle handle);

  void CloseHandle(MojoHandle handle, Dart_Port port, bool pruning = false);

  void UpdateTimer(int64_t deadline, Dart_Port port);

  void Shutdown();

  void RemoveHandleAtIndex(intptr_t i);

  void ProcessControlMessage();

  void ProcessTimers();

  void ProcessWaitManyResults(MojoResult result, uint32_t result_index);

  void PruneClosedHandles(bool signals_state_is_valid);

  void CompleteNextTimer();

  bool HasTimers();

  int64_t NextTimerDeadline();

  int64_t WaitDeadline();

  bool shutdown_;

  MojoHandle control_pipe_consumer_handle_;

  // All of these vectors are indexed together.
  std::vector<MojoHandle> wait_many_handles_;
  std::vector<MojoHandleSignals> wait_many_signals_;
  std::vector<MojoHandleSignalsState> wait_many_signals_state_;
  std::vector<Dart_Port> handle_ports_;

  // Map from MojoHandle -> index into above arrays.
  std::unordered_map<MojoHandle, intptr_t> handle_to_index_map_;

  // Set of timers sorted by earliest deadline.
  std::set<HandleWatcherTimer> timers_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(HandleWatcherThreadState);
};

HandleWatcherThreadState::HandleWatcherThreadState(
    MojoHandle control_pipe_consumer_handle)
    : shutdown_(false),
      control_pipe_consumer_handle_(control_pipe_consumer_handle) {
  MOJO_CHECK(control_pipe_consumer_handle_ != MOJO_HANDLE_INVALID);
  // Add the control handle.
  AddHandle(control_pipe_consumer_handle_,
            MOJO_HANDLE_SIGNAL_READABLE,
            ILLEGAL_PORT);
}

HandleWatcherThreadState::~HandleWatcherThreadState() {
  if (control_pipe_consumer_handle_ != MOJO_HANDLE_INVALID) {
    MojoClose(control_pipe_consumer_handle_);
    control_pipe_consumer_handle_ = MOJO_HANDLE_INVALID;
  }
}

void HandleWatcherThreadState::AddHandle(MojoHandle handle,
                                         MojoHandleSignals signals,
                                         Dart_Port port) {
  const size_t index = wait_many_handles_.size();
  MojoHandleSignalsState signals_state =
      { MOJO_HANDLE_SIGNAL_NONE, MOJO_HANDLE_SIGNAL_NONE};

  auto it = handle_to_index_map_.find(handle);
  if (it != handle_to_index_map_.end()) {
    intptr_t index = it->second;
    // Sanity check.
    MOJO_CHECK(wait_many_handles_[index] == handle);
    // We only support 1:1 mapping from handles to ports.
    if (handle_ports_[index] != port) {
      MOJO_LOG(ERROR) << "(Dart Handle Watcher) "
                      << "Handle " << handle << " is already bound!";
      PostSignal(port, MOJO_HANDLE_SIGNAL_PEER_CLOSED);
      return;
    }
    // Adjust the signals for this handle.
    wait_many_signals_[index] |= signals;
  } else {
    // New handle.
    wait_many_handles_.push_back(handle);
    wait_many_signals_.push_back(signals);
    wait_many_signals_state_.push_back(signals_state);
    handle_ports_.push_back(port);
    handle_to_index_map_[handle] = index;
  }

  // Sanity check.
  MOJO_CHECK(wait_many_handles_.size() == handle_to_index_map_.size());
}

void HandleWatcherThreadState::RemoveHandle(MojoHandle handle) {
  auto it = handle_to_index_map_.find(handle);

  // Removal of a handle for an incoming event can race with the removal of
  // a handle for an unsubscribe() call on the Dart MojoEventSubscription.
  // This is not an error, so we ignore attempts to remove a handle that is not
  // in the map.
  if (it == handle_to_index_map_.end()) {
    return;
  }
  const intptr_t index = it->second;
  // We should never be removing the control handle.
  MOJO_CHECK(index != CONTROL_HANDLE_INDEX);
  RemoveHandleAtIndex(index);
}

void HandleWatcherThreadState::CloseHandle(MojoHandle handle,
                                           Dart_Port port,
                                           bool pruning) {
  MOJO_CHECK(!pruning || (port == ILLEGAL_PORT));
  auto it = handle_to_index_map_.find(handle);
  if (it == handle_to_index_map_.end()) {
    // An app isolate may request that the handle watcher close a handle that
    // has already been pruned. This happens when the app isolate has not yet
    // received the PEER_CLOSED event. The app isolate will not close the
    // handle, so we must do so here.
    MojoClose(handle);
    if (port != ILLEGAL_PORT) {
      // Notify that close is done.
      PostNull(port);
    }
    return;
  }
  MojoClose(handle);
  if (port != ILLEGAL_PORT) {
    // Notify that close is done.
    PostNull(port);
  }
  const intptr_t index = it->second;
  MOJO_CHECK(index != CONTROL_HANDLE_INDEX);
  if (pruning) {
    // If this handle is being pruned, notify the application isolate
    // by sending PEER_CLOSED;
    PostSignal(handle_ports_[index], MOJO_HANDLE_SIGNAL_PEER_CLOSED);
  }
  // Remove the handle.
  RemoveHandle(handle);
}

void HandleWatcherThreadState::UpdateTimer(int64_t deadline, Dart_Port port) {
  // Scan the timers to see if we have a timer with |port|.
  auto it = timers_.begin();
  while (it != timers_.end()) {
    if (it->port == port) {
      break;
    }
    it++;
  }

  // We found an existing timer with |port|. Remove it.
  if (it != timers_.end()) {
    timers_.erase(it);
  }

  if (deadline < 0) {
    // A negative deadline means we should cancel this timer completely.
    return;
  }

  // Create a new timer with the current deadline.
  HandleWatcherTimer timer;
  timer.deadline = deadline;
  timer.port = port;

  timers_.insert(timer);
}

void HandleWatcherThreadState::Shutdown() {
  // Break out of the loop by setting the shutdown_ to true.
  shutdown_ = true;
}

void HandleWatcherThreadState::RemoveHandleAtIndex(intptr_t index) {
  MOJO_CHECK(index != CONTROL_HANDLE_INDEX);
  const intptr_t last_index = wait_many_handles_.size() - 1;

  // Remove handle from handle map.
  handle_to_index_map_.erase(wait_many_handles_[index]);

  if (index != last_index) {
    // We should never be overwriting CONTROL_HANDLE_INDEX.

    MojoHandle handle = wait_many_handles_[last_index];

    // Replace |index| with |last_index|.
    wait_many_handles_[index] = wait_many_handles_[last_index];
    wait_many_signals_[index] = wait_many_signals_[last_index];
    wait_many_signals_state_[index] = wait_many_signals_state_[last_index];
    handle_ports_[index] = handle_ports_[last_index];

    // Update handle map.
    handle_to_index_map_[handle] = index;
  }

  wait_many_handles_.pop_back();
  wait_many_signals_.pop_back();
  wait_many_signals_state_.pop_back();
  handle_ports_.pop_back();
  MOJO_CHECK(wait_many_handles_.size() >= 1);

  // Sanity check.
  MOJO_CHECK(wait_many_handles_.size() == handle_to_index_map_.size());
}

void HandleWatcherThreadState::ProcessControlMessage() {
  HandleWatcherCommand command = HandleWatcherCommand::Empty();
  uint32_t num_bytes = sizeof(command);
  uint32_t num_handles = 0;
  MojoResult res = MojoReadMessage(control_pipe_consumer_handle_,
                                   reinterpret_cast<void*>(&command),
                                   &num_bytes,
                                   nullptr,
                                   &num_handles,
                                   0);
  // Sanity check that we received the expected amount of data.
  MOJO_CHECK(res == MOJO_RESULT_OK);
  MOJO_CHECK(num_bytes == sizeof(command));
  MOJO_CHECK(num_handles == 0);
  switch (command.command()) {
    case HandleWatcherCommand::kCommandAddHandle:
      AddHandle(command.handle(), command.signals(), command.port());
    break;
    case HandleWatcherCommand::kCommandRemoveHandle:
      RemoveHandle(command.handle());
    break;
    case HandleWatcherCommand::kCommandCloseHandle:
      CloseHandle(command.handle(), command.port());
    break;
    case HandleWatcherCommand::kCommandAddTimer:
      UpdateTimer(command.deadline(), command.port());
    break;
    case HandleWatcherCommand::kCommandShutdownHandleWatcher:
      Shutdown();
    break;
    default:
      MOJO_CHECK(false);
    break;
  }
}

// Dart's Timer class uses MojoCoreNatives.timerMillisecondClock(), which
// calls MojoGetTimeTicksNow() and divides by 1000;
static int64_t GetDartTimeInMillis() {
  MojoTimeTicks ticks = MojoGetTimeTicksNow();
  return static_cast<int64_t>(ticks) / 1000;
}

void HandleWatcherThreadState::ProcessTimers() {
  int64_t now = GetDartTimeInMillis();
  while (HasTimers() && now >= NextTimerDeadline()) {
    CompleteNextTimer();
    now = GetDartTimeInMillis();
  }
}

void HandleWatcherThreadState::CompleteNextTimer() {
  auto it = timers_.begin();
  MOJO_CHECK(it != timers_.end());
  // Notify that the timer is complete.
  PostNull(it->port);
  // Remove it from the timer set.
  timers_.erase(it);
}

bool HandleWatcherThreadState::HasTimers() {
  return !timers_.empty();
}

int64_t HandleWatcherThreadState::NextTimerDeadline() {
  auto it = timers_.begin();
  MOJO_CHECK(it != timers_.end());
  return it->deadline;
}

int64_t HandleWatcherThreadState::WaitDeadline() {
  if (!HasTimers()) {
    // No pending timers. Wait indefinitely.
    return MOJO_DEADLINE_INDEFINITE;
  }
  int64_t now = GetDartTimeInMillis();
  return (NextTimerDeadline() - now) * 1000;
}

static bool ShouldCloseHandle(MojoHandle handle) {
  if (handle == MOJO_HANDLE_INVALID) {
    return false;
  }
  // Call wait with a deadline of 0. If the result of this is OK or
  // DEADLINE_EXCEEDED, the handle is still open.
  MojoResult result = MojoWait(handle, MOJO_HANDLE_SIGNAL_ALL, 0, NULL);
  return (result != MOJO_RESULT_OK) &&
         (result != MOJO_SYSTEM_RESULT_DEADLINE_EXCEEDED);
}

void HandleWatcherThreadState::PruneClosedHandles(bool signals_state_is_valid) {
  std::vector<MojoHandle> closed_handles;
  const intptr_t num_handles = wait_many_handles_.size();
  if (signals_state_is_valid) {
    // We can rely on |wait_many_signals_state_| having valid data.
    for (intptr_t i = 0; i < num_handles; i++) {
      // Check if the handle at index |i| has been closed.
      MojoHandleSignals satisfied_signals =
          wait_many_signals_state_[i].satisfied_signals;
      if ((satisfied_signals & MOJO_HANDLE_SIGNAL_PEER_CLOSED) != 0) {
        closed_handles.push_back(wait_many_handles_[i]);
      }
    }
  } else {
    // We can't rely on |wait_many_signals_state_| having valid data. So
    // we call Wait on each handle and check the status.
    for (intptr_t i = 0; i < num_handles; i++) {
      MojoHandle handle = wait_many_handles_[i];
      if (ShouldCloseHandle(handle)) {
        closed_handles.push_back(handle);
      }
    }
  }

  // Process all closed handles and notify their ports.
  for (size_t i = 0; i < closed_handles.size(); i++) {
    MojoHandle handle = closed_handles[i];
    CloseHandle(handle, ILLEGAL_PORT, true);
  }
}

void HandleWatcherThreadState::ProcessWaitManyResults(MojoResult result,
                                                      uint32_t result_index) {
  MOJO_CHECK(result != MOJO_SYSTEM_RESULT_DEADLINE_EXCEEDED);
  if (result != MOJO_RESULT_OK) {
    // The WaitMany call failed. We need to prune closed handles from our
    // wait many set and try again.
    //
    // If the result is an invalid argument |wait_many_signals_state_| is
    // meaningless.
    PruneClosedHandles(result != MOJO_SYSTEM_RESULT_INVALID_ARGUMENT);
    return;
  }
  MOJO_CHECK(result == MOJO_RESULT_OK);

  // Indexes of handles that we are done with.
  std::vector<intptr_t> to_remove;

  const intptr_t num_handles = wait_many_handles_.size();

  // Loop over all handles except for the control handle.
  // The order of the looping matters because we call RemoveHandleAtIndex
  // and need the handle indexes to start at the highest and decrease.
  for (intptr_t i = num_handles - 1; i > 0; i--) {
    MojoHandleSignals signals = wait_many_signals_[i];
    MojoHandleSignals satisfied_signals =
        wait_many_signals_state_[i].satisfied_signals;
    satisfied_signals &= signals;
    if (satisfied_signals != 0) {
      // Something happened to this handle.

      // Notify the port.
      PostSignal(handle_ports_[i], satisfied_signals);

      // Now that we have notified the waiting Dart program, remove this handle
      // from the wait many set until we are requested to add it again.
      to_remove.push_back(i);
    }
  }

  // Remove any handles we are finished with.
  const intptr_t num_to_remove = to_remove.size();
  for (intptr_t i = 0; i < num_to_remove; i++) {
    RemoveHandleAtIndex(to_remove[i]);
  }

  // Now check for control messages.
  {
    MojoHandleSignals signals = wait_many_signals_[CONTROL_HANDLE_INDEX];
    MojoHandleSignals satisfied_signals =
        wait_many_signals_state_[CONTROL_HANDLE_INDEX].satisfied_signals;
    satisfied_signals &= signals;
    if (satisfied_signals != 0) {
      // We have a control message.
      ProcessControlMessage();
    }
  }
}

void HandleWatcherThreadState::Run() {
  while (!shutdown_) {
    // Process timers.
    ProcessTimers();
    // Wait for the next timer or an event on a handle.
    uint32_t result_index = -1;
    uint32_t num_handles = wait_many_handles_.size();
    MOJO_CHECK(wait_many_signals_.size() == num_handles);
    MojoResult result = MojoWaitMany(wait_many_handles_.data(),
                                     wait_many_signals_.data(),
                                     num_handles,
                                     WaitDeadline(),
                                     &result_index,
                                     wait_many_signals_state_.data());

    if (result == MOJO_SYSTEM_RESULT_DEADLINE_EXCEEDED) {
      // Timers are ready.
      continue;
    }

    // Process wait results.
    ProcessWaitManyResults(result, result_index);
  }

  // Close our end of the message pipe.
  MojoClose(control_pipe_consumer_handle_);
}

std::unordered_map<MojoHandle, std::thread*>
    HandleWatcher::handle_watcher_threads_;
std::mutex HandleWatcher::handle_watcher_threads_mutex_;

// Create a message pipe for communication and spawns a handle watcher thread.
MojoHandle HandleWatcher::Start() {
  MojoCreateMessagePipeOptions options;
  options.struct_size = sizeof(MojoCreateMessagePipeOptions);
  options.flags = MOJO_CREATE_MESSAGE_PIPE_OPTIONS_FLAG_NONE;

  MojoHandle control_pipe_consumer_handle = MOJO_HANDLE_INVALID;
  MojoHandle control_pipe_producer_handle = MOJO_HANDLE_INVALID;
  MojoResult res = MojoCreateMessagePipe(&options,
                                         &control_pipe_consumer_handle,
                                         &control_pipe_producer_handle);
  if (res != MOJO_RESULT_OK) {
    return MOJO_HANDLE_INVALID;
  }

  // Spawn thread and pass both ends of the pipe to it.
  std::thread* thread = new std::thread(
      ThreadMain, control_pipe_consumer_handle);

  {
    std::lock_guard<std::mutex> lock(handle_watcher_threads_mutex_);
    // Record the thread object so that we can join on it during shutdown.
    MOJO_CHECK(handle_watcher_threads_.find(control_pipe_producer_handle) ==
               handle_watcher_threads_.end());
    handle_watcher_threads_[control_pipe_producer_handle] = thread;
  }

  // Return producer end of pipe to caller.
  return control_pipe_producer_handle;
}

void HandleWatcher::ThreadMain(MojoHandle control_pipe_consumer_handle) {
  HandleWatcherThreadState state(control_pipe_consumer_handle);

  // Run the main loop. When this returns the handle watcher has exited.
  state.Run();
}

MojoResult HandleWatcher::SendCommand(MojoHandle control_pipe_producer_handle,
                                      const HandleWatcherCommand& command) {
  return MojoWriteMessage(control_pipe_producer_handle,
                          reinterpret_cast<const void*>(&command),
                          sizeof(command),
                          nullptr,
                          0,
                          0);
}

std::thread* HandleWatcher::RemoveLocked(MojoHandle handle) {
  std::thread* t;
  auto mapping = handle_watcher_threads_.find(handle);
  if (mapping == handle_watcher_threads_.end()) {
    return nullptr;
  }
  t = mapping->second;
  handle_watcher_threads_.erase(handle);
  return t;
}

void HandleWatcher::Stop(MojoHandle control_pipe_producer_handle) {
  std::thread *t;
  {
    std::lock_guard<std::mutex> lock(handle_watcher_threads_mutex_);
    t = RemoveLocked(control_pipe_producer_handle);
  }

  if (t == nullptr) {
    return;
  }

  SendCommand(control_pipe_producer_handle, HandleWatcherCommand::Shutdown());
  t->join();

  MojoClose(control_pipe_producer_handle);
  delete t;
}

void HandleWatcher::StopLocked(MojoHandle handle) {
  std::thread *t = RemoveLocked(handle);
  MOJO_CHECK(t != nullptr);

  SendCommand(handle, HandleWatcherCommand::Shutdown());
  t->join();

  MojoClose(handle);
  delete t;
}

void HandleWatcher::StopAll() {
  std::lock_guard<std::mutex> lock(handle_watcher_threads_mutex_);

  std::vector<MojoHandle> control_handles;
  control_handles.reserve(handle_watcher_threads_.size());

  for (const auto& it : handle_watcher_threads_) {
    control_handles.push_back(it.first);
  }

  for (auto it : control_handles) {
    StopLocked(it);
  }
}

}  // namespace dart
}  // namespace mojo
