// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/job.h"

namespace zxdb {

Job::Job(Session* session) : ClientObject(session), weak_factory_(this) {}
Job::~Job() = default;

fxl::WeakPtr<Job> Job::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

}  // namespace zxdb
