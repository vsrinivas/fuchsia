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

#include <mojo/mojo_string.h>
#include <mojo/mojo_types.h>

static struct {
    mojo_result_t result;
    const char* message;
} messages[] = {
    { MOJO_RESULT_CANCELLED, "Operation cancelled" },
    { MOJO_RESULT_NOT_FOUND, "Resource not found" },
    { MOJO_RESULT_FAILED_PRECONDITION, "Failed precondition" },
    { MOJO_RESULT_INTERNAL, "Internal Error" },
    { MOJO_RESULT_RESOURCE_EXHAUSTED, "Not enough resources" },
    { MOJO_RESULT_INVALID_ARGUMENT, "Invalid argument" },
    { MOJO_RESULT_ABORTED, "Aborted" },
    { MOJO_RESULT_DEADLINE_EXCEEDED, "Timed out" },
    { MOJO_RESULT_ALREADY_EXISTS, "Resource already exists" },
    { MOJO_RESULT_UNAVAILABLE, "Resource unavailabe" },
    { MOJO_RESULT_PERMISSION_DENIED, "Permission denied" },
    { MOJO_RESULT_UNIMPLEMENTED, "Not implemented" },
    { MOJO_RESULT_OUT_OF_RANGE, "Out of range" },
    { MOJO_RESULT_DATA_LOSS, "Possible data loss" },
    { MOJO_RESULT_BUSY, "Resource busy" },
    { MOJO_RESULT_SHOULD_WAIT, "Should wait" },
    { MOJO_RESULT_UNKNOWN, "Unknown error" },
    { MOJO_RESULT_OK, "Success" },
};

const char* mojo_strerror(mojo_result_t result) {
    for (size_t idx = 0; idx < sizeof(messages) / sizeof(*messages); idx++)
        if (result == messages[idx].result)
            return messages[idx].message;
    return "No error information";
}
