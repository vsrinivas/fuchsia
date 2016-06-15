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

#include <runtime/status.h>

static struct {
    mx_status_t status;
    const char* string;
} table[] = {
    { NO_ERROR, "NO_ERROR" },
    { ERR_GENERIC, "ERR_GENERIC" },
    { ERR_NOT_FOUND, "ERR_NOT_FOUND" },
    { ERR_NOT_READY, "ERR_NOT_READY" },
    { ERR_NO_MSG, "ERR_NO_MSG" },
    { ERR_NO_MEMORY, "ERR_NO_MEMORY" },
    { ERR_ALREADY_STARTED, "ERR_ALREADY_STARTED" },
    { ERR_NOT_VALID, "ERR_NOT_VALID" },
    { ERR_INVALID_ARGS, "ERR_INVALID_ARGS" },
    { ERR_NOT_ENOUGH_BUFFER, "ERR_NOT_ENOUGH_BUFFER" },
    { ERR_NOT_SUSPENDED, "ERR_NOT_SUSPENDED" },
    { ERR_OBJECT_DESTROYED, "ERR_OBJECT_DESTROYED" },
    { ERR_NOT_BLOCKED, "ERR_NOT_BLOCKED" },
    { ERR_TIMED_OUT, "ERR_TIMED_OUT" },
    { ERR_ALREADY_EXISTS, "ERR_ALREADY_EXISTS" },
    { ERR_CHANNEL_CLOSED, "ERR_CHANNEL_CLOSED" },
    { ERR_OFFLINE, "ERR_OFFLINE" },
    { ERR_NOT_ALLOWED, "ERR_NOT_ALLOWED" },
    { ERR_BAD_PATH, "ERR_BAD_PATH" },
    { ERR_ALREADY_MOUNTED, "ERR_ALREADY_MOUNTED" },
    { ERR_IO, "ERR_IO" },
    { ERR_NOT_DIR, "ERR_NOT_DIR" },
    { ERR_NOT_FILE, "ERR_NOT_FILE" },
    { ERR_RECURSE_TOO_DEEP, "ERR_RECURSE_TOO_DEEP" },
    { ERR_NOT_SUPPORTED, "ERR_NOT_SUPPORTED" },
    { ERR_TOO_BIG, "ERR_TOO_BIG" },
    { ERR_CANCELLED, "ERR_CANCELLED" },
    { ERR_NOT_IMPLEMENTED, "ERR_NOT_IMPLEMENTED" },
    { ERR_CHECKSUM_FAIL, "ERR_CHECKSUM_FAIL" },
    { ERR_CRC_FAIL, "ERR_CRC_FAIL" },
    { ERR_CMD_UNKNOWN, "ERR_CMD_UNKNOWN" },
    { ERR_BAD_STATE, "ERR_BAD_STATE" },
    { ERR_BAD_LEN, "ERR_BAD_LEN" },
    { ERR_BUSY, "ERR_BUSY" },
    { ERR_THREAD_DETACHED, "ERR_THREAD_DETACHED" },
    { ERR_I2C_NACK, "ERR_I2C_NACK" },
    { ERR_ALREADY_EXPIRED, "ERR_ALREADY_EXPIRED" },
    { ERR_OUT_OF_RANGE, "ERR_OUT_OF_RANGE" },
    { ERR_NOT_CONFIGURED, "ERR_NOT_CONFIGURED" },
    { ERR_NOT_MOUNTED, "ERR_NOT_MOUNTED" },
    { ERR_FAULT, "ERR_FAULT" },
    { ERR_NO_RESOURCES, "ERR_NO_RESOURCES" },
    { ERR_BAD_HANDLE, "ERR_BAD_HANDLE" },
    { ERR_ACCESS_DENIED, "ERR_ACCESS_DENIED" },
    { ERR_PARTIAL_WRITE, "ERR_PARTIAL_WRITE" },
    { ERR_BAD_SYSCALL, "ERR_BAD_SYSCALL" },
};

const char* mx_strstatus(mx_status_t status) {
    for (unsigned idx = 0; idx < sizeof(table) / sizeof(*table); idx++)
        if (table[idx].status == status)
            return table[idx].string;

    return "No such mx_status_t";
}
