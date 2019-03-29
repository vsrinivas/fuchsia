// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_ZXDB_CLIENT_JOB_H_
#define GARNET_BIN_ZXDB_CLIENT_JOB_H_

#include <stdint.h>

#include <vector>

#include "garnet/bin/zxdb/client/client_object.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class JobContext;

class JobFilter {
  // TODO: decide structure
};

class Job : public ClientObject {
 public:
  Job(Session* session);
  ~Job() override;

  fxl::WeakPtr<Job> GetWeakPtr();

  // Returns the context associated with this job. Guaranteed non-null.
  virtual JobContext* GetJobContext() const = 0;

  // The Job koid is guaranteed non-null.
  virtual uint64_t GetKoid() const = 0;

  // Returns the "name" of the job.
  virtual const std::string& GetName() const = 0;

  // Get all filters from this job.
  const std::vector<JobFilter>& GetFilters() const { return filters_; };

  // Add filter to this job
  void AddFilter(std::string filter);

  // Remove filter from this job
  JobFilter RemoveFilter(uint32_t index);

 protected:
  std::vector<JobFilter> filters_;

 private:
  fxl::WeakPtrFactory<Job> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Job);
};

}  // namespace zxdb

#endif  // GARNET_BIN_ZXDB_CLIENT_JOB_H_
