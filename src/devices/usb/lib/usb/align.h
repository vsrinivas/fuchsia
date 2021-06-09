// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_LIB_USB_ALIGN_H_
#define SRC_DEVICES_USB_LIB_USB_ALIGN_H_

#define USB_ROUNDUP(a, b)       \
  ({                            \
    const __typeof(a) _a = (a); \
    const __typeof(b) _b = (b); \
    ((_a + _b - 1) / _b * _b);  \
  })
#define USB_ROUNDDOWN(a, b)     \
  ({                            \
    const __typeof(a) _a = (a); \
    const __typeof(b) _b = (b); \
    _a - (_a % _b);             \
  })

#endif  // SRC_DEVICES_USB_LIB_USB_ALIGN_H_
