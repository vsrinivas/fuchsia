// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified, Unaligned};

// A helper for unaligned reads of types that require alignment
#[repr(C, packed)]
#[derive(FromBytes, Unaligned)]
struct UnalignedWrapper<T> {
    value: T,
}

pub struct UnalignedView<B, T>(LayoutVerified<B, UnalignedWrapper<T>>);

impl<B, T> UnalignedView<B, T>
where
    B: ByteSlice,
    T: FromBytes,
{
    pub fn new(bytes: B) -> Option<Self> {
        Some(Self(LayoutVerified::new_unaligned(bytes)?))
    }

    pub fn get(&self) -> T
    where
        T: Copy,
    {
        self.0.value
    }
}

impl<B, T> UnalignedView<B, T>
where
    B: ByteSliceMut,
    T: AsBytes,
{
    pub fn set(&mut self, value: T) {
        self.0.bytes_mut().copy_from_slice(value.as_bytes());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    pub fn get() {
        let bytes = [1u8, 2, 3, 4];
        let view = UnalignedView::<_, u32>::new(&bytes[..]).expect("expected Ok");
        assert_eq!(0x04030201, view.get());
    }

    #[test]
    pub fn set() {
        let mut bytes = [1u8, 2, 3, 4];
        let mut view = UnalignedView::<_, u32>::new(&mut bytes[..]).expect("expected Ok");
        view.set(0x08070605);
        assert_eq!(0x08070605, view.get());
        assert_eq!(&[5, 6, 7, 8], &bytes[..]);
    }
}
