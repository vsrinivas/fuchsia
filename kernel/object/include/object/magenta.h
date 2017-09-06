// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/types.h>
#include <mxtl/ref_ptr.h>

class JobDispatcher;
class PolicyManager;
class ProcessDispatcher;

mxtl::RefPtr<JobDispatcher> GetRootJobDispatcher();

PolicyManager* GetSystemPolicyManager();

// Convenience function to get go from process handle to process.
mx_status_t get_process(ProcessDispatcher* up,
                        mx_handle_t proc_handle,
                        mxtl::RefPtr<ProcessDispatcher>* proc);
