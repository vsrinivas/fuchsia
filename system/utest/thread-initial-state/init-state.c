// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <assert.h>
#include <stdio.h>
#include <magenta/syscalls.h>

extern int thread_entry(void *arg);

int print_fail(void)
{
    printf("Failed\n");
    _magenta_thread_exit();
    return 1; // Not reached
}

int main(void)
{
    void *arg = (void *)0x1234567890abcdef;
    mx_handle_t handle = _magenta_thread_create(thread_entry, arg, "", 0);
    assert(handle >= 0);
    mx_status_t status = _magenta_handle_wait_one(handle, MX_SIGNAL_SIGNALED,
    	                                          MX_TIME_INFINITE, NULL, NULL);
    assert(status >= 0);
    return 0;
}
