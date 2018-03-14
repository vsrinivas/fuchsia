// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_impl.h"

#include "garnet/bin/zxdb/client/process_impl.h"

namespace zxdb {

ThreadImpl::ThreadImpl(ProcessImpl* process, uint64_t koid,
                       const std::string& name)
    : Thread(process->session()), process_(process), koid_(koid), name_(name) {}
ThreadImpl::~ThreadImpl() = default;

Process* ThreadImpl::GetProcess() const { return process_; }
uint64_t ThreadImpl::GetKoid() const { return koid_; }
const std::string& ThreadImpl::GetName() const { return name_; }

}  // namespace zxdb
