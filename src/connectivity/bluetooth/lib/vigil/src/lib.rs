// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub trait DropWatch<T: ?Sized> {
    /// Adds a function to be called just before this item is dropped.
    /// The function takes a reference to the item being dropped to avoid collision.
    /// /// # Example
    ///
    /// ```
    /// use vigil::DropWatch;
    ///
    /// let x = Vigil::new(5);
    ///
    /// DropWatch::watch(&v, |it| print!("{} about to be dropped!", it));
    /// ```
    fn watch(this: &Self, drop_fn: impl FnOnce(&T) + 'static);

    /// Makes a Dropped struct, which is a Future that completes just before the item is dropped.
    fn dropped(this: &Self) -> Dropped {
        Dropped::new(this)
    }
}

pub mod vigil;
pub use vigil::Vigil;

pub mod dropped;
pub use dropped::Dropped;
