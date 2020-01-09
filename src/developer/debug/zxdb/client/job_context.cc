// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/job_context.h"

namespace zxdb {

JobContext::JobContext(Session* session) : ClientObject(session), weak_factory_(this) {}
JobContext::~JobContext() = default;

fxl::WeakPtr<JobContext> JobContext::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

}  // namespace zxdb
