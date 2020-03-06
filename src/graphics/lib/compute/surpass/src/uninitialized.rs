// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    mem::{self, MaybeUninit},
    ptr,
};

#[inline]
pub fn write<T>(uninit: &mut MaybeUninit<T>, val: T) {
    unsafe {
        ptr::write(uninit.as_mut_ptr(), val);
    }
}

pub trait UninitializedVec<T> {
    fn resize_uninit(&mut self, len: usize);
    unsafe fn assume_init(&self) -> &[T];
    unsafe fn assume_init_mut(&mut self) -> &mut [T];
}

impl<T> UninitializedVec<T> for Vec<MaybeUninit<T>> {
    fn resize_uninit(&mut self, len: usize) {
        // Vec::reserve grows the Vec according to its length, not capacity, so we force the Vec to
        // have the same length as its capacity.
        let capacity = self.capacity();
        unsafe {
            // Safe because:
            //   * new length is equal to capacity
            //   * MaybeUninit<T> elements can be uninitialized
            self.set_len(capacity);
        }

        if capacity < len {
            self.reserve(len - capacity); // New capacity will be at least len.
        }

        unsafe {
            // Safe because:
            //   * because of the reserve call above, len is smaller or equal to the capacity
            //   * MaybeUninit<T> elements can be uninitialized
            self.set_len(len);
        }
    }

    unsafe fn assume_init(&self) -> &[T] {
        mem::transmute(&self[..])
    }

    unsafe fn assume_init_mut(&mut self) -> &mut [T] {
        mem::transmute(&mut self[..])
    }
}
