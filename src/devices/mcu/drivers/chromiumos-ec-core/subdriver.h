// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

namespace chromiumos_ec_core {

class ChromiumosEcCore;

void BindSubdrivers(ChromiumosEcCore* ec);

namespace motion {

void RegisterMotionDriver(ChromiumosEcCore* ec);

}  // namespace motion

namespace usb_pd {

void RegisterUsbPdDriver(ChromiumosEcCore* ec);

}

}  // namespace chromiumos_ec_core
