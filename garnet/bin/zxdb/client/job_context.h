// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ZXDB_CLIENT_JOB_CONTEXT_H_
#define GARNET_BIN_ZXDB_CLIENT_JOB_CONTEXT_H_

#include <functional>
#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/bin/zxdb/client/setting_store.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Err;
class Job;

// A JobContext represents the abstract idea of a job that can be debugged.
// This is as opposed to a Job which corresponds to one running job.
//
// Generally upon startup there would be a JobContext but no Job. This
// JobContext would receive the job name, koid, and other state from the user.
// Running this job_context would create the associated Job object. When the job
// exits, the JobContext can be re-used to launch the Job again with the same
// configuration.
class JobContext : public ClientObject {
 public:
  // Note that the callback will be issued in all cases which may be after the
  // job_context is destroyed. In this case the weak pointer will be null.
  using Callback =
      std::function<void(fxl::WeakPtr<JobContext> job_context, const Err&)>;

  enum State {
    // There is no job currently running. From here, it can only transition
    // to starting.
    kNone,

    // A pending state when the job has been requested to be started but
    // there is no reply from the debug agent yet. From here, it can transition
    // to running (success) or stopped (if launching or attaching failed).
    kStarting,

    // A pending state like starting but when we're waiting to attach.
    kAttaching,

    // The job is running. From here, it can only transition to stopped.
    kRunning
  };

  ~JobContext() override;

  fxl::WeakPtr<JobContext> GetWeakPtr();

  // Returns the current job state.
  virtual State GetState() const = 0;

  // Returns the job object if it is currently running (see GetState()).
  // Returns null otherwise.
  virtual Job* GetJob() const = 0;

  // Attaches to the job with the given koid. The callback will be
  // executed when the attach is complete (or fails).
  virtual void Attach(uint64_t koid, Callback callback) = 0;

  // Attaches to the component's root job., in which all the component's are
  // created. The callback will be executed when the attach is complete (or
  // fails).
  virtual void AttachToComponentRoot(Callback callback) = 0;

  // Detaches from the job with the given koid. The callback will be
  // executed when the detach is complete (or fails).
  virtual void Detach(Callback callback) = 0;

  // Provides the setting schema for this object.
  static fxl::RefPtr<SettingSchema> GetSchema();

  SettingStore& settings() { return settings_; }

 protected:
  explicit JobContext(Session* session);

  SettingStore settings_;

 private:
  fxl::WeakPtrFactory<JobContext> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(JobContext);
};

}  // namespace zxdb

#endif  // GARNET_BIN_ZXDB_CLIENT_JOB_CONTEXT_H_
