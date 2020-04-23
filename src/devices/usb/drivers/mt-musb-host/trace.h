// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_TRACE_H_
#define SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_TRACE_H_

namespace mt_usb_hci {

#define TRACE() zxlogf(TRACE, "[TRACE] %s", __PRETTY_FUNCTION__)

}  // namespace mt_usb_hci

#endif  // SRC_DEVICES_USB_DRIVERS_MT_MUSB_HOST_TRACE_H_
