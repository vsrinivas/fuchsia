// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ZXDB_CLIENT_JOB_CONTEXT_IMPL_H_
#define GARNET_BIN_ZXDB_CLIENT_JOB_CONTEXT_IMPL_H_

#include "garnet/bin/zxdb/client/job_context.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class JobImpl;
class SystemImpl;

class JobContextImpl : public JobContext {
 public:
  // The system owns this object and will outlive it.
  explicit JobContextImpl(SystemImpl* system);
  ~JobContextImpl() override;

  SystemImpl* system() { return system_; }
  JobImpl* job() { return job_.get(); }

  // Allocates a new job_context with the same settings as this one. This isn't
  // a real copy, because any job information is not cloned.
  std::unique_ptr<JobContextImpl> Clone(SystemImpl* system);

  // Removes the job from this job_context without making any IPC calls. This
  // can be used to clean up after a CreateJobForTesting(), and during
  // final shutdown. In final shutdown, we assume anything still left running
  // will continue running as-is and just clean up local references.
  //
  // If the job is not running, this will do nothing.
  void ImplicitlyDetach();

  // JobContext implementation:
  State GetState() const override;
  Job* GetJob() const override;
  void Attach(uint64_t koid, Callback callback) override;
  void AttachToComponentRoot(Callback callback) override;
  void Detach(Callback callback) override;

 private:
  SystemImpl* system_;  // Owns |this|.

  State state_ = kNone;

  // Associated job if there is one.
  std::unique_ptr<JobImpl> job_;

  fxl::WeakPtrFactory<JobContextImpl> impl_weak_factory_;

  void AttachInternal(debug_ipc::AttachRequest::Type type, uint64_t koid,
                      Callback callback);

  static void OnAttachReplyThunk(fxl::WeakPtr<JobContextImpl> job_context,
                                 Callback callback, const Err& err,
                                 uint64_t koid, uint32_t status,
                                 const std::string& job_name);
  void OnAttachReply(Callback callback, const Err& err, uint64_t koid,
                     uint32_t status, const std::string& job_name);
  void OnDetachReply(const Err& err, uint32_t status, Callback callback);

  FXL_DISALLOW_COPY_AND_ASSIGN(JobContextImpl);
};

}  // namespace zxdb

#endif  // GARNET_BIN_ZXDB_CLIENT_JOB_CONTEXT_IMPL_H_
