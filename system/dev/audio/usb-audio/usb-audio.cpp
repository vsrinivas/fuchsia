// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <string.h>

#include "usb-audio.h"

namespace audio {
namespace usb {

fbl::Array<uint8_t> FetchStringDescriptor(const usb_protocol_t& usb,
                                          uint8_t desc_id,
                                          uint16_t lang_id,
                                          uint16_t* out_lang_id) {
    uint8_t str_buf[512];
    size_t buflen = sizeof(str_buf);
    zx_status_t res = usb_get_string_descriptor(&usb, desc_id, &lang_id, str_buf, &buflen);

    if (out_lang_id) {
        *out_lang_id = lang_id;
    }

    if (res != ZX_OK) {
        return fbl::Array<uint8_t>();
    }

    buflen = fbl::min(buflen, sizeof(str_buf));

    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> mem(new (&ac) uint8_t[buflen + 1]);
    if (!ac.check()) {
        return fbl::Array<uint8_t>();
    }

    ::memcpy(mem.get(), str_buf, buflen);
    mem[buflen] = 0;

    return fbl::Array<uint8_t>(mem.release(), buflen);
}

}  // namespace usb
}  // namespace audio
