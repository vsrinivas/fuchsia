// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// LocallyInitiated is true when a disconnect (deauth/disassoc) is initiated from
/// within the device. If the disconnect is initiated remotely (i.e. by the AP if
/// device is a client, or by the client if device is an AP) due to a deauth or
/// disassoc frame, then LocallyInitiated is false.
pub struct LocallyInitiated(pub bool);
