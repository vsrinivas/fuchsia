// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/status.h>

static struct {
    mx_status_t status;
    const char* string;
} table[] = {
    {NO_ERROR, "NO_ERROR"},
    {ERR_INTERNAL, "ERR_INTERNAL"},
    {ERR_NOT_SUPPORTED, "ERR_NOT_SUPPORTED"},
    {ERR_NOT_FOUND, "ERR_NOT_FOUND"},
    {ERR_NO_MEMORY, "ERR_NO_MEMORY"},
    {ERR_NO_RESOURCES, "ERR_NO_RESOURCES"},
    {ERR_INVALID_ARGS, "ERR_INVALID_ARGS"},
    {ERR_WRONG_TYPE, "ERR_WRONG_TYPE"},
    {ERR_BAD_SYSCALL, "ERR_BAD_SYSCALL"},
    {ERR_BAD_HANDLE, "ERR_BAD_HANDLE"},
    {ERR_OUT_OF_RANGE, "ERR_OUT_OF_RANGE"},
    {ERR_NOT_ENOUGH_BUFFER, "ERR_NOT_ENOUGH_BUFFER"},
    {ERR_ALREADY_EXISTS, "ERR_ALREADY_EXISTS"},
    {ERR_ALREADY_BOUND, "ERR_ALREADY_BOUND"},
    {ERR_BAD_STATE, "ERR_BAD_STATE"},
    {ERR_FLOW_CONTROL, "ERR_FLOW_CONTROL"},
    {ERR_NOT_READY, "ERR_NOT_READY"},
    {ERR_TIMED_OUT, "ERR_TIMED_OUT"},
    {ERR_HANDLE_CLOSED, "ERR_HANDLE_CLOSED"},
    {ERR_CANCELLED, "ERR_CANCELLED"},
    {ERR_REMOTE_CLOSED, "ERR_REMOTE_CLOSED"},
    {ERR_NOT_AVAILABLE, "ERR_NOT_AVAILABLE"},
    {ERR_SHOULD_WAIT, "ERR_SHOULD_WAIT"},
    {ERR_ACCESS_DENIED, "ERR_ACCESS_DENIED"},
    {ERR_IO, "ERR_IO"},
    {ERR_I2C_NACK, "ERR_I2C_NACK"},
    {ERR_CHECKSUM_FAIL, "ERR_CHECKSUM_FAIL"},
    {ERR_BAD_PATH, "ERR_BAD_PATH"},
    {ERR_NOT_DIR, "ERR_NOT_DIR"},
    {ERR_NOT_FILE, "ERR_NOT_FILE"},
    {ERR_RECURSE_TOO_DEEP, "ERR_RECURSE_TOO_DEEP"},
};

const char* mx_strstatus(mx_status_t status) {
    for (unsigned idx = 0; idx < sizeof(table) / sizeof(*table); idx++)
        if (table[idx].status == status)
            return table[idx].string;

    return "No such mx_status_t";
}
