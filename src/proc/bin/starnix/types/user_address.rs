// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::sys::zx_vaddr_t;
use std::fmt;
use std::marker::PhantomData;
use std::mem;
use std::ops;
use zerocopy::{AsBytes, FromBytes};

use super::UserBuffer;
use crate::mm::vmo::round_up_to_increment;
use crate::types::Errno;

#[derive(Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd, AsBytes, FromBytes)]
#[repr(transparent)]
pub struct UserAddress(u64);

impl UserAddress {
    const NULL_PTR: u64 = 0;

    // TODO(lindkvist): Remove this in favor of marking the From<u64> trait const once feature is
    // stabilized.
    pub const fn from(value: u64) -> Self {
        UserAddress(value)
    }

    pub fn from_ptr(ptr: zx_vaddr_t) -> Self {
        UserAddress(ptr as u64)
    }

    pub fn ptr(&self) -> zx_vaddr_t {
        self.0 as zx_vaddr_t
    }

    pub fn round_up(&self, increment: u64) -> Result<UserAddress, Errno> {
        Ok(UserAddress(round_up_to_increment(self.0 as usize, increment as usize)? as u64))
    }

    pub fn is_aligned(&self, alignment: u64) -> bool {
        self.0 % alignment == 0
    }

    pub fn is_null(&self) -> bool {
        self.0 == UserAddress::NULL_PTR
    }

    pub fn checked_add(&self, rhs: usize) -> Option<UserAddress> {
        self.0.checked_add(rhs as u64).map(UserAddress)
    }
}

impl Default for UserAddress {
    fn default() -> UserAddress {
        UserAddress(UserAddress::NULL_PTR)
    }
}

impl Into<UserAddress> for u64 {
    fn into(self) -> UserAddress {
        UserAddress(self)
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

impl fmt::Debug for UserAddress {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_tuple("UserAddress").field(&format_args!("{:#x}", self.0)).finish()
    }
}

#[derive(Debug, Default, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
#[repr(transparent)]
pub struct UserRef<T> {
    addr: UserAddress,
    phantom: PhantomData<T>,
}

impl<T> UserRef<T> {
    pub fn new(addr: UserAddress) -> Self {
        Self { addr, phantom: PhantomData }
    }

    /// Returns None if the buffer is too small for the type.
    pub fn from_buf(buf: UserBuffer) -> Option<Self> {
        if mem::size_of::<T>() < buf.length {
            return None;
        }
        Some(Self::new(buf.address))
    }

    pub fn addr(&self) -> UserAddress {
        self.addr
    }

    pub fn next(&self) -> UserRef<T> {
        Self::new(self.addr() + mem::size_of::<T>())
    }

    pub fn at(&self, index: usize) -> Self {
        UserRef::<T>::new(self.addr() + index * mem::size_of::<T>())
    }

    pub fn cast<S>(&self) -> UserRef<S> {
        UserRef::<S>::new(self.addr)
    }
}

impl<T> From<UserAddress> for UserRef<T> {
    fn from(user_address: UserAddress) -> Self {
        Self::new(user_address)
    }
}

impl<T> ops::Deref for UserRef<T> {
    type Target = UserAddress;

    fn deref(&self) -> &UserAddress {
        &self.addr
    }
}

impl<T> fmt::Display for UserRef<T> {
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

#[cfg(test)]
mod tests {
    use super::{UserAddress, UserRef};

    #[::fuchsia::test]
    fn test_into() {
        assert_eq!(UserRef::<u32>::default(), UserAddress::default().into());
        let user_address = UserAddress::from(32);
        assert_eq!(UserRef::<i32>::new(user_address), user_address.into());
    }
}
