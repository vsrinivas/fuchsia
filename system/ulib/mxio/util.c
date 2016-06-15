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


#include <magenta/syscalls.h>

#include "util.h"

mx_handle_t mxu_read_handle(mx_handle_t h) {
    mx_signals_t pending;
    mx_handle_t out;
    mx_status_t r;
    uint32_t sz;

    for (;;) {
        sz = 1;
        r = _magenta_message_read(h, NULL, 0, &out, &sz, 0);
        if (r == 0) {
            return out;
        }
        if (r == ERR_NO_MSG) {
            r = _magenta_handle_wait_one(h, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                         MX_TIME_INFINITE, &pending, NULL);
            if (r < 0) return r;
            if (pending & MX_SIGNAL_READABLE) continue;
            if (pending & MX_SIGNAL_PEER_CLOSED) return ERR_CHANNEL_CLOSED;
            return ERR_GENERIC;
        }
        return r;
    }
}

mx_status_t mxu_blocking_read(mx_handle_t h, void* data, size_t len) {
    mx_signals_t pending;
    mx_status_t r;
    uint32_t sz;

    for (;;) {
        sz = len;
        r = _magenta_message_read(h, data, &sz, NULL, NULL, 0);
        if (r == 0) {
            return sz;
        }
        if (r == ERR_NO_MSG) {
            r = _magenta_handle_wait_one(h, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                         MX_TIME_INFINITE, &pending, NULL);
            if (r < 0) return r;
            if (pending & MX_SIGNAL_READABLE) continue;
            if (pending & MX_SIGNAL_PEER_CLOSED) return ERR_CHANNEL_CLOSED;
            return ERR_GENERIC;
        }
        return r;
    }
}

mx_status_t mxu_blocking_read_h(mx_handle_t h, void* data, size_t len, mx_handle_t* out) {
    mx_signals_t pending;
    mx_status_t r;
    uint32_t sz, hsz;

    for (;;) {
        sz = len;
        hsz = 1;
        r = _magenta_message_read(h, data, &sz, out, &hsz, 0);
        if (r == 0) {
            if (hsz != 1) *out = 0;
            return sz;
        }
        if (r == ERR_NO_MSG) {
            r = _magenta_handle_wait_one(h, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                         MX_TIME_INFINITE, &pending, NULL);
            if (r < 0) return r;
            if (pending & MX_SIGNAL_READABLE) continue;
            if (pending & MX_SIGNAL_PEER_CLOSED) return ERR_CHANNEL_CLOSED;
            return ERR_GENERIC;
        }
        return r;
    }
}
