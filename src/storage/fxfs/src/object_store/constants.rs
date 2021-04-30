// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Some places use Default and assume that zero is an invalid object ID, so this cannot be changed
// easily.
pub const INVALID_OBJECT_ID: u64 = 0;

// This only exists in the root store.
pub const SUPER_BLOCK_OBJECT_ID: u64 = 1;
