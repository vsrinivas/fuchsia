// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utility types to handle shared contexts.

use std::future::Future;
use std::ops::{Deref, DerefMut};

/// A type that provides futures-enabled locks to some type `T`.
pub(crate) trait Lockable<'a, T> {
    type Guard: Deref<Target = T> + DerefMut + Send;
    type Fut: Future<Output = Self::Guard> + Send;

    fn lock(&'a self) -> Self::Fut;
}
