// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementations for working with [Jobs](crate::job::Job) for incoming requests.

pub mod request;
pub mod watch;

/// [Scoped] is a simple wrapper that can be used to overcome issues with using types outside this
/// crate in traits that are defined outside as well. For example, the [From] trait requires that
/// genericized type be defined within the crate. The extract method can be used to retrieve the
/// contained data.
pub struct Scoped<T>(pub T);

impl<T> Scoped<T> {
    pub fn extract(self) -> T {
        self.0
    }
}
