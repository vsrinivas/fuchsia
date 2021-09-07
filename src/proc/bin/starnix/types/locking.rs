// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::sync::Arc;

/// Returns a `usize` that can be used to sort the provided `&Arc<Mutex>`.
fn sort_key<T>(m: &Arc<Mutex<T>>) -> usize {
    Arc::as_ptr(m) as usize
}

/// Sorts the provided `items` in a globablly consistent order with respect to associated mutexes.
///
/// This is useful when locking multiple items at once to avoid inconsistent orderings between
/// different callsites.
///
/// # Parameters
/// - `items`: The items that will be sorted.
/// - `lock_fn`: A function that, given an item from `items`, returns a `&Mutex` for that item.
/// - `T`: The type of the item.
/// - `M`: The type contained in the mutex.
/// - `F`: The type of the function that extracts the mutex from the item.
pub fn sort_for_locking<T, M, F>(items: &mut [&T], lock_fn: F)
where
    F: Fn(&T) -> &Arc<Mutex<M>>,
{
    items.sort_by(|item1, item2| {
        let mutex1 = lock_fn(item1);
        let mutex2 = lock_fn(item2);

        sort_key(mutex1).cmp(&sort_key(mutex2))
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Clone, Debug, Default)]
    struct MutexHolder {
        mutex: Arc<Mutex<u64>>,
        identifier: u64,
    }

    impl PartialEq for MutexHolder {
        fn eq(&self, other: &Self) -> bool {
            self.identifier == other.identifier
        }
    }

    #[test]
    fn test_lock_ordering() {
        let m1 = MutexHolder { mutex: Arc::new(Mutex::new(0)), identifier: 3 };
        let m2 = MutexHolder { mutex: Arc::new(Mutex::new(0)), identifier: 1 };
        let m3 = MutexHolder { mutex: Arc::new(Mutex::new(0)), identifier: 2 };

        let mut holders = [&m1, &m2, &m3];
        sort_for_locking(&mut holders, |m| &m.mutex);
        let first_order = holders.clone();

        let mut holders = [&m3, &m2, &m1];
        sort_for_locking(&mut holders, |m| &m.mutex);
        assert_eq!(holders, first_order);

        let mut holders = [&m2, &m3, &m1];
        sort_for_locking(&mut holders, |m| &m.mutex);
        assert_eq!(holders, first_order);

        let mut holders = [&m1, &m3, &m2];
        sort_for_locking(&mut holders, |m| &m.mutex);
        assert_eq!(holders, first_order);
    }
}
