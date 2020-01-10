// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_CONTEXT_H_

#include <string>
#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Err;
class Job;

// A JobContext represents the abstract idea of a job that can be debugged. This is as opposed to a
// Job which corresponds to one running job.
//
// Generally upon startup there would be a JobContext but no Job. This JobContext would receive the
// job name, koid, and other state from the user. Running this job_context would create the
// associated Job object. When the job exits, the JobContext can be re-used to launch the Job again
// with the same configuration.
//
// TODO(bug 43794) combine JobContext[Impl] and Job[Impl] objects.
class JobContext : public ClientObject {
 public:
  // Note that the callback will be issued in all cases which may be after the job_context is
  // destroyed. In this case the weak pointer will be null.
  using Callback = fit::callback<void(fxl::WeakPtr<JobContext> job_context, const Err&)>;

  enum class State {
    // There is no job currently running. From here, it can only transition to starting.
    kNone,

    // A pending state during the time we requested to be attached and when the reply from the
    // debug_agent comes back.
    kAttaching,

    // The job is attached. From here, it can only transition to none.
    kAttached
  };

  ~JobContext() override;

  fxl::WeakPtr<JobContext> GetWeakPtr();

  // Returns the current job state.
  virtual State GetState() const = 0;

  // Returns the job object if it is currently running (see GetState()). Returns null otherwise.
  virtual Job* GetJob() const = 0;

  // Attaches to the job with the given koid. The callback will be executed when the attach is
  // complete (or fails).
  virtual void Attach(uint64_t koid, Callback callback) = 0;

  // Attaches to the given special job. The root job is the system root, and the component job is
  // the one in which all the components are created. The callback will be executed when the attach
  // is complete (or fails).
  virtual void AttachToSystemRoot(Callback callback) = 0;
  virtual void AttachToComponentRoot(Callback callback) = 0;

  // Detaches from the job with the given koid. The callback will be executed when the detach is
  // complete (or fails).
  virtual void Detach(Callback callback) = 0;

 protected:
  explicit JobContext(Session* session);

 private:
  fxl::WeakPtrFactory<JobContext> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(JobContext);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_CONTEXT_H_
