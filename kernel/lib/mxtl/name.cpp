// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <mxtl/name.h>

#include <kernel/auto_lock.h>
#include <string.h>

void Name::get(char out_name[MX_MAX_NAME_LEN]) const {
    AutoSpinLock lock(lock_);
    memcpy(out_name, name_, MX_MAX_NAME_LEN);
}

mx_status_t Name::set(const char* name, size_t len) {
    if (len >= MX_MAX_NAME_LEN)
        len = MX_MAX_NAME_LEN - 1;

    AutoSpinLock lock(lock_);
    memcpy(name_, name, len);
    memset(name_ + len, 0, MX_MAX_NAME_LEN - len);
    return NO_ERROR;
}
