// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_H_

#include "lib/fit/function.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/client/filter_observer.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Err;

// A Job represents the abstract idea of a job that can be debugged.
// The job is attached if there's a corresponding job running and we have the koid and name for it.
class Job : public ClientObject, public FilterObserver {
 public:
  // Note that the callback will be issued in all cases which may be after the job is
  // destroyed. In this case the weak pointer will be null.
  using Callback = fit::callback<void(fxl::WeakPtr<Job> job, const Err&)>;

  enum class State {
    // There is no job currently running. From here, it can only transition to starting.
    kNone,

    // A pending state during the time we requested to be attached and when the reply from the
    // debug_agent comes back.
    kAttaching,

    // The job is attached. From here, it can only transition to none.
    kAttached
  };

  Job(Session* session, bool is_implicit_root);
  ~Job() override;
  fxl::WeakPtr<Job> GetWeakPtr();

  // The implicit root job is one created automatically on startup that's implicitly attached. This
  // job will be automatically reconnected if the connect is reconnected.
  //
  // If the job is explicitly detached, this flag will be cleared (because the user is taking
  // responsibility for where it's attached).
  bool is_implicit_root() const { return is_implicit_root_; }

  // Returns the current job state.
  State state() const { return state_; }

  // The following two are only valid when state_ == kAttached.
  uint64_t koid() const { return koid_; }
  const std::string& name() const { return name_; }

  // Attaches to the job with the given koid. The callback will be executed when the attach is
  // complete (or fails).
  void Attach(uint64_t koid, Callback callback);

  // Attaches to the given special job. The root job is the system root, and the component job is
  // the one in which all the components are created. The callback will be executed when the attach
  // is complete (or fails).
  void AttachToSystemRoot(Callback callback);
  void AttachToComponentRoot(Callback callback);

  // Attaches with the given koid and name without making IPC calls.
  void AttachForTesting(uint64_t koid, const std::string& name);

  // Detaches from the job. The callback will be executed when the detach is complete (or fails).
  void Detach(Callback callback);

  // Detaches without making any IPC calls. This can be used to clean up after AttachForTesting(),
  // and during final shutdown. In final shutdown, we assume anything still left running will
  // continue running as-is and just clean up local references.
  //
  // If the job is not running, this will do nothing.
  void ImplicitlyDetach();

  // FilterObserver implementation
  void DidCreateFilter(Filter* filter) override;
  void DidChangeFilter(Filter* filter, std::optional<Job*> previous_job) override;
  void WillDestroyFilter(Filter* filter) override;

  // Same as the two-argument version below but forces an update if the last one failed.
  // Made public because JobTest will use it.
  void SendAndUpdateFilters(std::vector<std::string> filters);

 private:
  State state_ = State::kNone;

  // Only valid when state_ is kAttached.
  uint64_t koid_ = 0;
  std::string name_;

  std::vector<std::string> filters_;
  bool is_implicit_root_ = false;
  bool last_filter_set_failed_ = false;

  fxl::WeakPtrFactory<Job> weak_factory_;

  void RefreshFilters();

  void AttachInternal(debug_ipc::TaskType type, uint64_t koid, Callback callback);

  static void OnAttachReplyThunk(fxl::WeakPtr<Job> job, Callback callback, const Err& err,
                                 uint64_t koid, const debug::Status& status,
                                 const std::string& job_name);
  void OnAttachReply(Callback callback, const Err& err, uint64_t koid, const debug::Status& status,
                     const std::string& job_name);
  void OnDetachReply(const Err& err, const debug::Status& status, Callback callback);

  // If job is running this will update |filters_| only after getting OK from agent else it will set
  // |filters_| and return.
  void SendAndUpdateFilters(std::vector<std::string> filters, bool force_send);

  FXL_DISALLOW_COPY_AND_ASSIGN(Job);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_H_
