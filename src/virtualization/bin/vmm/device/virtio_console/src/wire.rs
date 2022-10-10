// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// 5.3.2 Virtqueues
//
// Note that these queues are from the perspective of the driver, so the RX queue is for device
// to driver communication, etc.
pub const RX_QUEUE_IDX: u16 = 0;
pub const TX_QUEUE_IDX: u16 = 1;
