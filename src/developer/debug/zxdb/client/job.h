// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_H_

#include <stdint.h>

#include <vector>

#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class JobContext;

// Represents a running job the debugger is attached to.
//
// This is owned by the JobContext when it is attached.
//
// TODO(bug 43794) combine JobContext[Impl] and Job[Impl] objects.
class Job : public ClientObject {
 public:
  explicit Job(Session* session);
  ~Job() override;

  fxl::WeakPtr<Job> GetWeakPtr();

  // Returns the context associated with this job. Guaranteed non-null.
  virtual JobContext* GetJobContext() const = 0;

  // The Job koid is guaranteed non-null.
  virtual uint64_t GetKoid() const = 0;

  // Returns the "name" of the job.
  virtual const std::string& GetName() const = 0;

 private:
  fxl::WeakPtrFactory<Job> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Job);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_H_
