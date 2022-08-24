// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines multiple types of inspect containers: Mapped VMO for production and byte arrays
//! for testing.

use std::{cmp::min, ptr};

/// Trait implemented by an Inspect container that can be read from.
pub trait ReadableBlockContainer: Clone {
    /// Writes the container at the given `offset` into the given `bytes`.
    fn read_bytes(&self, offset: usize, bytes: &mut [u8]) -> usize;
    fn size(&self) -> usize;
}

/// Trait implemented by an Inspect container that can be written to.
pub trait WritableBlockContainer: Clone {
    /// Writes the given `bytes` at the given `offset` in the container.
    fn write_bytes(&self, offset: usize, bytes: &[u8]) -> usize;
}

/// Trait implemented to compare two inspect containers for equality.
pub trait BlockContainerEq<RHS = Self> {
    /// Returns true if the other container is the same.
    fn ptr_eq(&self, other: &RHS) -> bool;
}

impl ReadableBlockContainer for &[u8] {
    fn read_bytes(&self, offset: usize, bytes: &mut [u8]) -> usize {
        if offset >= self.len() {
            return 0;
        }
        let upper_bound = min(self.len(), bytes.len() + offset);
        let bytes_read = upper_bound - offset;
        bytes[..bytes_read].clone_from_slice(&self[offset..upper_bound]);
        bytes_read
    }

    fn size(&self) -> usize {
        self.len()
    }
}

impl BlockContainerEq for &[u8] {
    fn ptr_eq(&self, other: &&[u8]) -> bool {
        ptr::eq(*self, *other)
    }
}

#[cfg(target_os = "fuchsia")]
pub mod target {
    use crate::*;
    use mapped_vmo::Mapping;
    use std::sync::Arc;

    impl ReadableBlockContainer for Arc<Mapping> {
        fn read_bytes(&self, offset: usize, bytes: &mut [u8]) -> usize {
            self.read_at(offset, bytes) as usize
        }

        fn size(&self) -> usize {
            self.len()
        }
    }

    impl BlockContainerEq for Arc<Mapping> {
        fn ptr_eq(&self, other: &Arc<Mapping>) -> bool {
            Arc::ptr_eq(&self, &other)
        }
    }

    impl WritableBlockContainer for Arc<Mapping> {
        fn write_bytes(&self, offset: usize, bytes: &[u8]) -> usize {
            self.write_at(offset, bytes) as usize
        }
    }

    /// A type alias to the concrete type used as the data container in a production
    /// environment.
    ///
    /// On Fuchsia: Arc<Mapping>.
    /// On Host: Arc<Mutex<Vec<u8>>>.
    pub type Container = Arc<Mapping>;
}

#[cfg(not(target_os = "fuchsia"))]
pub mod target {
    use crate::*;
    use std::cmp::min;
    use std::sync::Arc;
    use std::sync::Mutex;

    impl ReadableBlockContainer for Arc<Mutex<Vec<u8>>> {
        fn read_bytes(&self, offset: usize, bytes: &mut [u8]) -> usize {
            if let Ok(locked) = self.lock() {
                (locked.as_ref() as &[u8]).read_bytes(offset, bytes)
            } else {
                0
            }
        }

        fn size(&self) -> usize {
            if let Ok(locked) = self.lock() {
                locked.len()
            } else {
                0
            }
        }
    }

    impl WritableBlockContainer for Arc<Mutex<Vec<u8>>> {
        fn write_bytes(&self, offset: usize, bytes: &[u8]) -> usize {
            if let Ok(mut locked) = self.lock() {
                let len = locked.len();
                let mut written = 0;
                for i in offset..min(len, offset + bytes.len()) {
                    locked[i] = bytes[written];
                    written += 1;
                }

                written
            } else {
                0
            }
        }
    }

    impl BlockContainerEq for Arc<Mutex<Vec<u8>>> {
        fn ptr_eq(&self, other: &Arc<Mutex<Vec<u8>>>) -> bool {
            if Arc::ptr_eq(self, other) {
                return true;
            }

            if let (Ok(lhs), Ok(rhs)) = (self.lock(), other.lock()) {
                return *lhs == *rhs;
            } else {
                return false;
            }
        }
    }

    /// A type alias to the concrete type used as the data container in a production
    /// environment.
    ///
    /// On Fuchsia: Arc<Mapping>.
    /// On Host: Arc<Mutex<Vec<u8>>>.
    pub type Container = Arc<Mutex<Vec<u8>>>;
}

pub use target::*;

#[cfg(test)]
impl WritableBlockContainer for &[u8] {
    fn write_bytes(&self, offset: usize, bytes: &[u8]) -> usize {
        if offset >= self.len() {
            return 0;
        }
        let bytes_written = min(self.len() - offset, bytes.len());
        let base = (self.as_ptr() as usize).checked_add(offset).unwrap() as *mut u8;
        unsafe { std::ptr::copy_nonoverlapping(bytes.as_ptr(), base, bytes_written) };
        bytes_written
    }
}
