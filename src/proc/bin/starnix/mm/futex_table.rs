// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::ops::DerefMut;
use std::sync::Arc;

use crate::task::*;
use crate::types::*;

/// An entry in a FutexEntryList.
struct FutexEntry {
    /// The underlying Waiter to notify when waking.
    waiter: Arc<Waiter>,

    /// The bitmask used with FUTEX_WAIT_BITSET and FUTEX_WAKE_BITSET.
    mask: u32,
}

/// An entry in a FutexTable.
///
/// Keeps a list of waiters waiting on the given futex.
#[derive(Default)]
struct FutexEntryList {
    /// The waiters waiting on this futex.
    entries: Arc<Mutex<Vec<FutexEntry>>>,
}

/// A table of futexes.
///
/// Each 32-bit aligned address in an address space can potentially have an
/// associated futex that userspace can wait upon. This table is a sparse
/// representation that has an actual FutexEntryList only for those addresses
/// that have ever actually had a futex operation performed on them.
#[derive(Default)]
pub struct FutexTable {
    /// The futexes associated with each address in the address space.
    ///
    /// This HashMap is populated on-demand when futexes are used.
    state: Mutex<HashMap<UserAddress, FutexEntryList>>,
}

impl FutexTable {
    /// Take the given number of waiters from the futex at the given address.
    ///
    /// Count can be any number, but is typically either 1 or usize::MAX.
    ///
    /// Callers can attempt to take more waiters than exist, but this function
    /// will return only the waiters that actually exist.
    ///
    /// Waiters are returned in FIFO order.
    fn take(&self, addr: UserAddress, count: usize, mask: u32) -> Vec<Arc<Waiter>> {
        let entries = {
            let mut state = self.state.lock();
            if let Some(list) = state.get_mut(&addr) {
                Arc::clone(&list.entries)
            } else {
                return vec![];
            }
        };
        let mut entries = entries.lock();
        let mut taken = vec![];
        let mut local = vec![];
        std::mem::swap(entries.deref_mut(), &mut local);
        *entries = local
            .into_iter()
            .filter(|entry| {
                if taken.len() < count && entry.mask & mask != 0 {
                    taken.push(Arc::clone(&entry.waiter));
                    return false;
                }
                return true;
            })
            .collect();
        return taken;
    }

    /// Wait on the futex at the given address.
    ///
    /// See FUTEX_WAIT.
    pub fn wait(
        &self,
        task: &Task,
        addr: UserAddress,
        value: u32,
        mask: u32,
        deadline: zx::Time,
    ) -> Result<(), Errno> {
        let user_current = UserRef::<u32>::new(addr);
        let mut current = 0;
        {
            let entries = self.get_entries(addr);
            let mut entries = entries.lock();
            // TODO: This read should be atomic.
            task.mm.read_object(user_current, &mut current)?;
            if current != value {
                return Ok(());
            }
            entries.push(FutexEntry { waiter: Arc::clone(&task.waiter), mask });
        }
        task.waiter.wait_util(deadline)
    }

    /// Wake the given number of waiters on futex at the given address.
    ///
    /// See FUTEX_WAKE.
    pub fn wake(&self, addr: UserAddress, count: usize, mask: u32) {
        for waiter in self.take(addr, count, mask).iter() {
            waiter.wake();
        }
    }

    /// Returns the list of futex entries for a given address.
    fn get_entries(&self, addr: UserAddress) -> Arc<Mutex<Vec<FutexEntry>>> {
        let mut state = self.state.lock();
        let list = state.entry(addr).or_insert_with(|| FutexEntryList::default());
        Arc::clone(&list.entries)
    }

    #[cfg(test)]
    fn add(&self, addr: UserAddress, entry: FutexEntry) {
        self.get_entries(addr).lock().push(entry);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_futex_table_take() {
        let addr = UserAddress::from(0xFF0000);
        let table = FutexTable::default();

        let waiters = table.take(addr, 2, FUTEX_BITSET_MATCH_ANY);
        assert_eq!(0, waiters.len());

        let waiter0 = Waiter::new();
        let waiter1 = Waiter::new();
        let waiter2 = Waiter::new();

        table.add(addr, FutexEntry { waiter: Arc::clone(&waiter0), mask: FUTEX_BITSET_MATCH_ANY });
        table.add(addr, FutexEntry { waiter: Arc::clone(&waiter1), mask: FUTEX_BITSET_MATCH_ANY });
        table.add(addr, FutexEntry { waiter: Arc::clone(&waiter2), mask: FUTEX_BITSET_MATCH_ANY });

        let waiters = table.take(addr, 2, FUTEX_BITSET_MATCH_ANY);
        assert_eq!(2, waiters.len());
        assert!(Arc::ptr_eq(&waiter0, &waiters[0]));
        assert!(Arc::ptr_eq(&waiter1, &waiters[1]));

        let waiters = table.take(addr, usize::MAX, FUTEX_BITSET_MATCH_ANY);
        assert_eq!(1, waiters.len());
        assert!(Arc::ptr_eq(&waiter2, &waiters[0]));

        let waiters = table.take(addr, 3, FUTEX_BITSET_MATCH_ANY);
        assert_eq!(0, waiters.len());
    }

    #[test]
    fn test_futex_mask() {
        let addr = UserAddress::from(0xFF0000);
        let table = FutexTable::default();

        let waiter0 = Waiter::new();
        let waiter1 = Waiter::new();
        let waiter2 = Waiter::new();

        table.add(addr, FutexEntry { waiter: Arc::clone(&waiter0), mask: 0x13 });
        table.add(addr, FutexEntry { waiter: Arc::clone(&waiter1), mask: 0x11 });
        table.add(addr, FutexEntry { waiter: Arc::clone(&waiter2), mask: 0x12 });

        let waiters = table.take(addr, 2, 0x2);
        assert_eq!(2, waiters.len());
        assert!(Arc::ptr_eq(&waiter0, &waiters[0]));
        assert!(Arc::ptr_eq(&waiter2, &waiters[1]));

        let waiters = table.take(addr, usize::MAX, 0x1);
        assert_eq!(1, waiters.len());
        assert!(Arc::ptr_eq(&waiter1, &waiters[0]));
    }
}
