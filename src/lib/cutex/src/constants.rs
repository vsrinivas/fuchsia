// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use static_assertions::const_assert;

pub(crate) const IS_LOCKED: usize = 1usize.rotate_right(1);
pub(crate) const HAS_WAITERS: usize = 1usize.rotate_right(2);

pub(crate) const SENTINEL: usize = !(IS_LOCKED | HAS_WAITERS);

const_assert!(SENTINEL | IS_LOCKED != SENTINEL);
const_assert!(SENTINEL | HAS_WAITERS != SENTINEL);
