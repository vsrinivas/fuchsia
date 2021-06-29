// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::mem;
use std::sync::Arc;

use crate::task::Waiter;
use crate::types::*;

/// An entry in a FutexTable.
///
/// Keeps a list of waiters waiting on the given futex.
#[derive(Default)]
struct FutexEntry {
    /// The waiters waiting on this futex.
    waiters: Vec<Arc<Waiter>>,
}

/// A table of futexes.
///
/// Each 32-bit aligned address in an address space can potentially have an
/// associated futex that userspace can wait upon. This table is a sparse
/// representation that has an actual FutexEntry only for those addresses that
/// have ever actually had a futex operation performed on them.
#[derive(Default)]
pub struct FutexTable {
    /// The futexes associated with each address in the address space.
    ///
    /// This HashMap is populated on-demand when futexes are used.
    state: Mutex<HashMap<UserAddress, FutexEntry>>,
}

impl FutexTable {
    /// Add the given waiter to the list of waiters at the given address.
    ///
    /// This operation is a separate function from wait() because we cannot
    /// hold the lock on the futex table while sleeping. If we did, another
    /// thread that wanted to wake that futex would not be able to read the
    /// futex table.
    fn add(&self, addr: UserAddress, waiter: &Arc<Waiter>) {
        let mut state = self.state.lock();
        let entry = state.entry(addr).or_insert_with(|| FutexEntry::default());
        entry.waiters.push(Arc::clone(waiter))
    }

    /// Take the given number of waiters from the futex at the given address.
    ///
    /// Count can be any number, but is typically either 1 or usize::MAX.
    ///
    /// Callers can attempt to take more waiters than exist, but this function
    /// will return only the waiters that actually exist.
    ///
    /// Waiters are returned in FIFO order.
    fn take(&self, addr: UserAddress, count: usize) -> Vec<Arc<Waiter>> {
        let mut state = self.state.lock();
        match state.get_mut(&addr) {
            None => vec![],
            Some(entry) => {
                let waiting = entry.waiters.len();
                if count >= waiting {
                    return mem::take(&mut entry.waiters);
                }
                let mut waiters = entry.waiters.split_off(count);
                mem::swap(&mut entry.waiters, &mut waiters);
                return waiters;
            }
        }
    }

    /// Wait on the futex at the given address.
    ///
    /// See FUTEX_WAIT.
    pub fn wait(
        &self,
        waiter: &Arc<Waiter>,
        addr: UserAddress,
        _value: u32,
        deadline: zx::Time,
    ) -> Result<(), Errno> {
        // TODO: Check _value against the contents of addr.
        self.add(addr, waiter);
        waiter.wait_util(deadline)
    }

    /// Wake the given number of waiters on futex at the given address.
    ///
    /// See FUTEX_WAKE.
    pub fn wake(&self, addr: UserAddress, count: usize) {
        for waiter in self.take(addr, count).iter() {
            waiter.wake();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_futex_table_take() {
        let addr = UserAddress::from(0xFF0000);
        let table = FutexTable::default();

        let waiters = table.take(addr, 2);
        assert_eq!(0, waiters.len());

        let waiter0 = Waiter::new();
        let waiter1 = Waiter::new();
        let waiter2 = Waiter::new();

        table.add(addr, &waiter0);
        table.add(addr, &waiter1);
        table.add(addr, &waiter2);

        let waiters = table.take(addr, 2);
        assert_eq!(2, waiters.len());
        assert!(Arc::ptr_eq(&waiter0, &waiters[0]));
        assert!(Arc::ptr_eq(&waiter1, &waiters[1]));

        let waiters = table.take(addr, usize::MAX);
        assert_eq!(1, waiters.len());
        assert!(Arc::ptr_eq(&waiter2, &waiters[0]));

        let waiters = table.take(addr, 3);
        assert_eq!(0, waiters.len());
    }
}
