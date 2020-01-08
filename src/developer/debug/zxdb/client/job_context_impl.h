// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_CONTEXT_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_CONTEXT_IMPL_H_

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/filter_observer.h"
#include "src/developer/debug/zxdb/client/job_context.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class JobImpl;
class SystemImpl;

class JobContextImpl : public JobContext, public SettingStoreObserver, public FilterObserver {
 public:
  // The system owns this object and will outlive it.
  JobContextImpl(SystemImpl* system, bool is_implicit_root);

  ~JobContextImpl() override;

  SystemImpl* system() { return system_; }
  JobImpl* job() { return job_.get(); }

  // The implicit root job is one created automatically on startup that's implicitly attached. This
  // job will be automatically reconnected if the connect is reconnected.
  //
  // If the job is explicitly detached, this flag will be cleared (because the user is taking
  // responsibility for where it's attached).
  bool is_implicit_root() const { return is_implicit_root_; }

  // Removes the job from this job_context without making any IPC calls. This can be used to clean
  // up after a CreateJobForTesting(), and during final shutdown. In final shutdown, we assume
  // anything still left running will continue running as-is and just clean up local references.
  //
  // If the job is not running, this will do nothing.
  void ImplicitlyDetach();

  // Same as the two-argument version below but forces an update if the last one failed.
  void SendAndUpdateFilters(std::vector<std::string> filters);

  // JobContext implementation:
  State GetState() const override;
  Job* GetJob() const override;
  void Attach(uint64_t koid, Callback callback) override;
  void AttachToSystemRoot(Callback callback) override;
  void AttachToComponentRoot(Callback callback) override;

  void AddJobImplForTesting(uint64_t koid, const std::string& name);

  void Detach(Callback callback) override;

  // SettingStoreObserver implementation
  void OnSettingChanged(const SettingStore&, const std::string& setting_name) override;

  // FilterObserver implementation
  void DidCreateFilter(Filter* filter) override;
  void OnChangedFilter(Filter* filter, std::optional<JobContext*> previous_job) override;
  void WillDestroyFilter(Filter* filter) override;

 private:
  SystemImpl* system_;  // Owns |this|.

  State state_ = State::kNone;

  // Associated job if there is one.
  std::unique_ptr<JobImpl> job_;
  std::vector<std::string> filters_;
  bool is_implicit_root_ = false;
  bool last_filter_set_failed_ = false;

  fxl::WeakPtrFactory<JobContextImpl> impl_weak_factory_;

  void RefreshFilters();

  void AttachInternal(debug_ipc::TaskType type, uint64_t koid, Callback callback);

  static void OnAttachReplyThunk(fxl::WeakPtr<JobContextImpl> job_context, Callback callback,
                                 const Err& err, uint64_t koid, uint32_t status,
                                 const std::string& job_name);
  void OnAttachReply(Callback callback, const Err& err, uint64_t koid, uint32_t status,
                     const std::string& job_name);
  void OnDetachReply(const Err& err, uint32_t status, Callback callback);

  // If job is running this will update |filters_| only after getting OK from agent else it will set
  // |filters_| and return.
  void SendAndUpdateFilters(std::vector<std::string> filters, bool force_send);

  FXL_DISALLOW_COPY_AND_ASSIGN(JobContextImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_CONTEXT_IMPL_H_
