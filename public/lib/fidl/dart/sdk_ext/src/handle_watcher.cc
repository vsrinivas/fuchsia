// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/dart/sdk_ext/src/handle_watcher.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "dart/runtime/include/dart_api.h"
#include "dart/runtime/include/dart_native_api.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_point.h"

namespace fidl {
namespace dart {

constexpr int kControlHandleIndex = 0;

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
  HandleWatcherThreadState(mx::channel consumer_handle);

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

  void AddHandle(mx_handle_t handle, mx_signals_t signals, Dart_Port port);

  void RemoveHandle(mx_handle_t handle);

  void CloseHandle(mx_handle_t handle, Dart_Port port, bool pruning = false);

  void UpdateTimer(int64_t deadline, Dart_Port port);

  void Shutdown();

  void RemoveHandleAtIndex(intptr_t i);

  void ProcessControlMessage();

  void ProcessTimers();

  void ProcessWaitManyResults();

  void PruneClosedHandles();

  void CompleteNextTimer();

  bool HasTimers();

  int64_t NextTimerDeadline();

  int64_t WaitDeadline();

  bool shutdown_;

  mx::channel consumer_handle_;

  // All of these vectors are indexed together.
  std::vector<mx_wait_item_t> wait_many_items_;
  std::vector<Dart_Port> handle_ports_;

  // Map from mx_handle_t -> index into above arrays.
  std::unordered_map<mx_handle_t, intptr_t> handle_to_index_map_;

  // Set of timers sorted by earliest deadline.
  std::set<HandleWatcherTimer> timers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HandleWatcherThreadState);
};

HandleWatcherThreadState::HandleWatcherThreadState(mx::channel consumer_handle)
    : shutdown_(false), consumer_handle_(std::move(consumer_handle)) {
  FTL_CHECK(consumer_handle_);
  // Add the control handle.
  AddHandle(consumer_handle_.get(), MX_SIGNAL_READABLE, ILLEGAL_PORT);
}

HandleWatcherThreadState::~HandleWatcherThreadState() {}

void HandleWatcherThreadState::AddHandle(mx_handle_t handle,
                                         mx_signals_t signals,
                                         Dart_Port port) {
  const size_t index = wait_many_items_.size();

  auto it = handle_to_index_map_.find(handle);
  if (it != handle_to_index_map_.end()) {
    intptr_t index = it->second;
    // Sanity check.
    FTL_CHECK(wait_many_items_[index].handle == handle);
    // We only support 1:1 mapping from handles to ports.
    if (handle_ports_[index] != port) {
      FTL_LOG(ERROR) << "(Dart Handle Watcher) "
                     << "Handle " << handle << " is already bound!";
      PostSignal(port, MX_SIGNAL_PEER_CLOSED);
      return;
    }
    // Adjust the signals for this handle.
    wait_many_items_[index].waitfor |= signals;
  } else {
    // New handle.
    mx_wait_item_t wait_item = {handle, signals, 0u};
    wait_many_items_.emplace_back(wait_item);
    handle_ports_.push_back(port);
    handle_to_index_map_[handle] = index;
  }

  // Sanity check.
  FTL_CHECK(wait_many_items_.size() == handle_to_index_map_.size());
}

void HandleWatcherThreadState::RemoveHandle(mx_handle_t handle) {
  auto it = handle_to_index_map_.find(handle);

  // Removal of a handle for an incoming event can race with the removal of
  // a handle for an unsubscribe() call on the Dart HandleWaiter.
  // This is not an error, so we ignore attempts to remove a handle that is not
  // in the map.
  if (it == handle_to_index_map_.end()) {
    return;
  }
  const intptr_t index = it->second;
  // We should never be removing the control handle.
  FTL_CHECK(index != kControlHandleIndex);
  RemoveHandleAtIndex(index);
}

void HandleWatcherThreadState::CloseHandle(mx_handle_t handle,
                                           Dart_Port port,
                                           bool pruning) {
  FTL_CHECK(!pruning || (port == ILLEGAL_PORT));
  auto it = handle_to_index_map_.find(handle);
  if (it == handle_to_index_map_.end()) {
    // An app isolate may request that the handle watcher close a handle that
    // has already been pruned. This happens when the app isolate has not yet
    // received the PEER_CLOSED event. The app isolate will not close the
    // handle, so we must do so here.
    mx_handle_close(handle);
    if (port != ILLEGAL_PORT) {
      // Notify that close is done.
      PostNull(port);
    }
    return;
  }
  mx_handle_close(handle);
  if (port != ILLEGAL_PORT) {
    // Notify that close is done.
    PostNull(port);
  }
  const intptr_t index = it->second;
  FTL_CHECK(index != kControlHandleIndex);
  if (pruning) {
    // If this handle is being pruned, notify the application isolate
    // by sending PEER_CLOSED;
    PostSignal(handle_ports_[index], MX_SIGNAL_PEER_CLOSED);
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
  FTL_CHECK(index != kControlHandleIndex);
  const intptr_t last_index = wait_many_items_.size() - 1;

  // Remove handle from handle map.
  handle_to_index_map_.erase(wait_many_items_[index].handle);

  if (index != last_index) {
    // We should never be overwriting kControlHandleIndex.

    mx_handle_t handle = wait_many_items_[last_index].handle;

    // Replace |index| with |last_index|.
    wait_many_items_[index] = wait_many_items_[last_index];
    handle_ports_[index] = handle_ports_[last_index];

    // Update handle map.
    handle_to_index_map_[handle] = index;
  }

  wait_many_items_.pop_back();
  handle_ports_.pop_back();
  FTL_CHECK(wait_many_items_.size() >= 1);

  // Sanity check.
  FTL_CHECK(wait_many_items_.size() == handle_to_index_map_.size());
}

void HandleWatcherThreadState::ProcessControlMessage() {
  HandleWatcherCommand command = HandleWatcherCommand::Empty();
  uint32_t num_bytes = sizeof(command);
  uint32_t num_handles = 0;
  mx_status_t res =
      consumer_handle_.read(0, reinterpret_cast<void*>(&command), num_bytes,
                            &num_bytes, nullptr, 0, &num_handles);
  // Sanity check that we received the expected amount of data.
  FTL_CHECK(res == NO_ERROR);
  FTL_CHECK(num_bytes == sizeof(command));
  FTL_CHECK(num_handles == 0);
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
      FTL_NOTREACHED();
      break;
  }
}

static int64_t GetDartTimeInMillis() {
  return ftl::TimePoint::Now().ToEpochDelta().ToMilliseconds();
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
  FTL_CHECK(it != timers_.end());
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
  FTL_CHECK(it != timers_.end());
  return it->deadline;
}

int64_t HandleWatcherThreadState::WaitDeadline() {
  if (!HasTimers()) {
    // No pending timers. Wait indefinitely.
    return MX_TIME_INFINITE;
  }
  int64_t now = GetDartTimeInMillis();
  return (NextTimerDeadline() - now) * 1000;
}

static bool ShouldCloseHandle(mx_handle_t handle) {
  if (handle == MX_HANDLE_INVALID)
    return false;
  mx_status_t result = mx_handle_wait_one(handle, 0, 0, nullptr);
  return (result == ERR_HANDLE_CLOSED);
}

void HandleWatcherThreadState::PruneClosedHandles() {
  std::vector<mx_handle_t> closed_handles;
  const intptr_t num_handles = wait_many_items_.size();

  for (intptr_t i = 0; i < num_handles; i++) {
    mx_handle_t handle = wait_many_items_[i].handle;
    if (ShouldCloseHandle(handle)) {
      closed_handles.push_back(handle);
    }
  }

  // Process all closed handles and notify their ports.
  for (size_t i = 0; i < closed_handles.size(); i++) {
    mx_handle_t handle = closed_handles[i];
    CloseHandle(handle, ILLEGAL_PORT, true);
  }
}

void HandleWatcherThreadState::ProcessWaitManyResults() {
  // Indexes of handles that we are done with.
  std::vector<intptr_t> to_remove;

  const intptr_t num_handles = wait_many_items_.size();

  // Loop over all handles except for the control handle.
  // The order of the looping matters because we call RemoveHandleAtIndex
  // and need the handle indexes to start at the highest and decrease.
  for (intptr_t i = num_handles - 1; i > 0; i--) {
    mx_signals_t waitfor = wait_many_items_[i].waitfor;
    mx_signals_t pending = wait_many_items_[i].pending;
    if (pending & waitfor) {
      // Something happened to this handle.

      // Notify the port.
      PostSignal(handle_ports_[i], pending);

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
    mx_signals_t waitfor = wait_many_items_[kControlHandleIndex].waitfor;
    mx_signals_t pending = wait_many_items_[kControlHandleIndex].pending;
    if (pending & waitfor) {
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
    uint32_t num_handles = wait_many_items_.size();
    FTL_CHECK(wait_many_items_.size() == num_handles);
    mx_status_t result = mx_handle_wait_many(wait_many_items_.data(),
                                             num_handles, WaitDeadline());

    if (result == ERR_BAD_HANDLE) {
      PruneClosedHandles();
      continue;
    }

    FTL_CHECK(result == NO_ERROR || result == ERR_TIMED_OUT)
        << "mx_handle_wait_many returned unexpected result: " << result;
    ProcessWaitManyResults();
  }

  // Close our end of the channel.
  consumer_handle_.reset();
}

std::unordered_map<mx_handle_t, std::thread>
    HandleWatcher::handle_watcher_threads_;
std::mutex HandleWatcher::handle_watcher_threads_mutex_;

// Create a channel for communication and spawns a handle watcher thread.
mx::channel HandleWatcher::Start() {
  mx::channel consumer_handle;
  mx::channel producer_handle;
  mx_status_t res = mx::channel::create(0, &consumer_handle, &producer_handle);
  if (res != NO_ERROR)
    return mx::channel();

  // Spawn thread and pass one end of the channel to it.
  std::thread thread(ThreadMain, consumer_handle.release());

  {
    std::lock_guard<std::mutex> lock(handle_watcher_threads_mutex_);
    // Record the thread object so that we can join on it during shutdown.
    FTL_CHECK(handle_watcher_threads_.find(producer_handle.get()) ==
              handle_watcher_threads_.end());
    handle_watcher_threads_[producer_handle.get()] = std::move(thread);
  }

  // Return producer end of channel to caller.
  return producer_handle;
}

void HandleWatcher::ThreadMain(mx_handle_t consumer_handle) {
  HandleWatcherThreadState state((mx::channel(consumer_handle)));

  // Run the main loop. When this returns the handle watcher has exited.
  state.Run();
}

mx_status_t HandleWatcher::SendCommand(mx_handle_t producer_handle,
                                       const HandleWatcherCommand& command) {
  return mx_channel_write(producer_handle, 0,
                          reinterpret_cast<const void*>(&command),
                          sizeof(command), nullptr, 0);
}

std::thread HandleWatcher::RemoveLocked(mx_handle_t producer_handle) {
  std::thread result;
  auto it = handle_watcher_threads_.find(producer_handle);
  if (it == handle_watcher_threads_.end())
    return std::thread();
  result.swap(it->second);
  handle_watcher_threads_.erase(it);
  return result;
}

void HandleWatcher::Stop(mx::channel producer_handle) {
  std::thread thread;
  {
    std::lock_guard<std::mutex> lock(handle_watcher_threads_mutex_);
    thread = RemoveLocked(producer_handle.get());
  }

  if (!thread.joinable())
    return;

  SendCommand(producer_handle.get(), HandleWatcherCommand::Shutdown());
  thread.join();
}

void HandleWatcher::StopLocked(mx_handle_t producer_handle) {
  std::thread thread = RemoveLocked(producer_handle);
  FTL_CHECK(thread.joinable());

  SendCommand(producer_handle, HandleWatcherCommand::Shutdown());
  thread.join();

  mx_handle_close(producer_handle);
}

void HandleWatcher::StopAll() {
  std::lock_guard<std::mutex> lock(handle_watcher_threads_mutex_);

  std::vector<mx_handle_t> control_handles;
  control_handles.reserve(handle_watcher_threads_.size());

  for (const auto& it : handle_watcher_threads_) {
    control_handles.push_back(it.first);
  }

  for (auto it : control_handles) {
    StopLocked(it);
  }
}

}  // namespace dart
}  // namespace fidl
