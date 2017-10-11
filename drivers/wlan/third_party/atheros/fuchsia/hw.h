/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 * Copyright (c) 2017 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#define ATHEROS_VID      0x168c

#define QCA988X_2_0_DID  0x003c
#define QCA6174_2_1_DID  0x003e
#define QCA99X0_2_0_DID  0x0040
#define QCA6164_2_1_DID  0x0041
#define QCA9377_1_0_DID  0x0042
#define QCA9984_1_0_DID  0x0046
#define QCA9887_1_0_DID  0x0050
#define QCA9888_2_0_DID  0x0056

#ifdef __cplusplus

namespace ath10k {

// Hardware revisions don't line up exactly with the device ids (e.g., QCA6164 has HwRev QCA6174).
enum class HwRev {
    UNKNOWN,
    QCA988X,
    QCA6174,
    QCA99X0,
    QCA9377,
    QCA9984,
    QCA9887,
    QCA9888,
};

const char* HwRevToString(HwRev rev);

}  // namespace ath10k
#endif  // __cplusplus
