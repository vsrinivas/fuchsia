// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::sys::zx_vaddr_t;
use std::fmt;
use std::marker::PhantomData;
use std::mem;
use std::ops;
use zerocopy::{AsBytes, FromBytes};

use crate::mm::vmo::round_up_to_increment;

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd, AsBytes, FromBytes)]
#[repr(transparent)]
pub struct UserAddress(u64);

impl UserAddress {
    const NULL_PTR: u64 = 0;

    pub fn from(value: u64) -> Self {
        UserAddress(value)
    }

    pub fn from_ptr(ptr: zx_vaddr_t) -> Self {
        UserAddress(ptr as u64)
    }

    pub fn ptr(&self) -> zx_vaddr_t {
        self.0 as zx_vaddr_t
    }

    pub fn round_up(&self, increment: u64) -> UserAddress {
        UserAddress(round_up_to_increment(self.0 as usize, increment as usize) as u64)
    }

    pub fn is_null(&self) -> bool {
        self.0 == UserAddress::NULL_PTR
    }
}

impl Default for UserAddress {
    fn default() -> UserAddress {
        UserAddress(UserAddress::NULL_PTR)
    }
}

impl ops::Add<u32> for UserAddress {
    type Output = UserAddress;

    fn add(self, rhs: u32) -> UserAddress {
        UserAddress(self.0 + (rhs as u64))
    }
}

impl ops::Add<u64> for UserAddress {
    type Output = UserAddress;

    fn add(self, rhs: u64) -> UserAddress {
        UserAddress(self.0 + rhs)
    }
}

impl ops::Add<usize> for UserAddress {
    type Output = UserAddress;

    fn add(self, rhs: usize) -> UserAddress {
        UserAddress(self.0 + (rhs as u64))
    }
}

impl ops::Sub<u32> for UserAddress {
    type Output = UserAddress;

    fn sub(self, rhs: u32) -> UserAddress {
        UserAddress(self.0 - (rhs as u64))
    }
}

impl ops::Sub<u64> for UserAddress {
    type Output = UserAddress;

    fn sub(self, rhs: u64) -> UserAddress {
        UserAddress(self.0 - rhs)
    }
}

impl ops::Sub<usize> for UserAddress {
    type Output = UserAddress;

    fn sub(self, rhs: usize) -> UserAddress {
        UserAddress(self.0 - (rhs as u64))
    }
}

impl ops::AddAssign<usize> for UserAddress {
    fn add_assign(&mut self, rhs: usize) {
        *self = *self + rhs;
    }
}

impl ops::SubAssign<usize> for UserAddress {
    fn sub_assign(&mut self, rhs: usize) {
        *self = *self - rhs;
    }
}

impl ops::Sub<UserAddress> for UserAddress {
    type Output = usize;

    fn sub(self, rhs: UserAddress) -> usize {
        self.ptr() - rhs.ptr()
    }
}

impl fmt::Display for UserAddress {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:#x}", self.0)
    }
}

#[derive(Debug, Default, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
#[repr(transparent)]
pub struct UserRef<T: AsBytes + FromBytes> {
    addr: UserAddress,
    phatom: PhantomData<T>,
}

impl<T: AsBytes + FromBytes> UserRef<T> {
    pub fn new(addr: UserAddress) -> UserRef<T> {
        UserRef::<T> { addr, phatom: PhantomData::<T>::default() }
    }

    pub fn addr(&self) -> UserAddress {
        self.addr
    }

    pub fn len(&self) -> usize {
        mem::size_of::<T>()
    }

    pub fn next(&self) -> UserRef<T> {
        Self::new(self.addr() + mem::size_of::<T>())
    }
}

impl<T: AsBytes + FromBytes> ops::Deref for UserRef<T> {
    type Target = UserAddress;

    fn deref(&self) -> &UserAddress {
        &self.addr
    }
}

impl<T: AsBytes + FromBytes> fmt::Display for UserRef<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.addr().fmt(f)
    }
}

#[derive(Debug, Default, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd, AsBytes, FromBytes)]
#[repr(transparent)]
pub struct UserCString(UserAddress);

impl UserCString {
    pub fn new(addr: UserAddress) -> UserCString {
        UserCString(addr)
    }

    pub fn addr(&self) -> UserAddress {
        self.0
    }
}

impl ops::Deref for UserCString {
    type Target = UserAddress;

    fn deref(&self) -> &UserAddress {
        &self.0
    }
}

impl fmt::Display for UserCString {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.addr().fmt(f)
    }
}
