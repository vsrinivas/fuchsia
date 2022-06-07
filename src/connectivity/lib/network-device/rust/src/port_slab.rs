// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Data structure helper to keep data associated with netdevice ports.

use crate::Port;

#[derive(Debug)]
struct Slot<T> {
    salt: u8,
    value: T,
}

/// A data structure that is keyed on [`Port`], guarantees O(1) lookup
/// and takes into account salted port identifiers.
#[derive(Debug, derivative::Derivative)]
#[derivative(Default(bound = ""))]
pub struct PortSlab<T> {
    slots: Vec<Option<Slot<T>>>,
}

/// Observable outcomes from [`PortSlab::remove`].
#[derive(Eq, PartialEq, Debug)]
pub enum RemoveOutcome<T> {
    /// Requested port was not present.
    PortNotPresent,
    /// There exists a port with the same ID present, but the stored salt
    /// doesn't match. Contains the stored salt.
    SaltMismatch(u8),
    /// Port was removed, contains the stored value.
    Removed(T),
}

impl<T> PortSlab<T> {
    /// Creates a new empty `PortSlab`.
    pub fn new() -> Self {
        Self::default()
    }

    /// Inserts `value` indexed by `port`.
    ///
    /// Returns `Some(_)` if the value is inserted and there was already
    /// some value stored in the slab.
    pub fn insert(&mut self, port: Port, value: T) -> Option<T> {
        let Port { base, salt } = port;
        let slot = self.get_slot_mut(base.into());
        slot.replace(Slot { salt, value }).map(|Slot { salt: _, value }| value)
    }

    /// Removes the entry indexed by `port`, if one exists.
    ///
    /// Note that `remove` will *not* remove an entry if the currently stored
    /// port's salt doesn't match `port`.
    pub fn remove(&mut self, port: &Port) -> RemoveOutcome<T> {
        match self.entry(*port) {
            Entry::SaltMismatch(SaltMismatchEntry(slot)) => {
                RemoveOutcome::SaltMismatch(slot.as_ref().unwrap().salt)
            }
            Entry::Vacant(VacantEntry(_, _)) => RemoveOutcome::PortNotPresent,
            Entry::Occupied(e) => RemoveOutcome::Removed(e.remove()),
        }
    }

    /// Gets a reference to the value indexed by `port`.
    ///
    /// `get` only returns `Some` if the slab contains an entry for `port` with
    /// a matching salt.
    pub fn get(&self, port: &Port) -> Option<&T> {
        let Self { slots } = self;
        let Port { base, salt } = port;
        slots
            .get(usize::from(*base))
            .and_then(|s| s.as_ref())
            .and_then(|Slot { salt: existing_salt, value }| (existing_salt == salt).then(|| value))
    }

    /// Gets a mutable reference to the value indexed by `port`.
    ///
    /// `get_mut` only returns `Some` if the slab contains an entry for `port`
    /// with a matching salt.
    pub fn get_mut(&mut self, port: &Port) -> Option<&mut T> {
        let Self { slots } = self;
        let Port { base, salt } = port;
        slots
            .get_mut(usize::from(*base))
            .and_then(|s| s.as_mut())
            .and_then(|Slot { salt: existing_salt, value }| (existing_salt == salt).then(|| value))
    }

    /// Retrieves an [`entry`] indexed by `port`.
    pub fn entry(&mut self, port: Port) -> Entry<'_, T> {
        let Port { base, salt } = port;
        let base = usize::from(base);

        // NB: Lifetimes in this function disallow us from doing the "pretty"
        // thing here of matching just once on the result of `get_mut`. We need
        // to erase the lifetime information with the boolean check to appease
        // the borrow checker. Otherwise we get errors of the form
        // "error[E0499]: cannot borrow `*self` as mutable more than once at a
        // time".
        if self.slots.get_mut(base).is_none() {
            return Entry::Vacant(VacantEntry(
                VacantState::NeedSlot(self, usize::from(base)),
                salt,
            ));
        }
        let slot = self.slots.get_mut(base).unwrap();
        match slot {
            Some(Slot { salt: existing_salt, value: _ }) => {
                if *existing_salt == salt {
                    Entry::Occupied(OccupiedEntry(slot))
                } else {
                    Entry::SaltMismatch(SaltMismatchEntry(slot))
                }
            }
            None => Entry::Vacant(VacantEntry(VacantState::EmptySlot(slot), salt)),
        }
    }

    fn get_slot_mut(&mut self, index: usize) -> &mut Option<Slot<T>> {
        let Self { slots } = self;
        // The slab only ever grows.
        if slots.len() <= index {
            slots.resize_with(index + 1, || None);
        }

        &mut slots[index]
    }
}

/// An entry obtained from [`PortSlab::entry`].
#[derive(Debug)]
pub enum Entry<'a, T> {
    /// Slot is vacant.
    Vacant(VacantEntry<'a, T>),
    /// Slot is occupied with a matching salt.
    Occupied(OccupiedEntry<'a, T>),
    /// Slot is occupied with a mismatched salt.
    SaltMismatch(SaltMismatchEntry<'a, T>),
}

#[derive(Debug)]
enum VacantState<'a, T> {
    NeedSlot(&'a mut PortSlab<T>, usize),
    EmptySlot(&'a mut Option<Slot<T>>),
}

/// A vacant slot in a [`PortSlab`].
#[derive(Debug)]
pub struct VacantEntry<'a, T>(VacantState<'a, T>, u8);

impl<'a, T> VacantEntry<'a, T> {
    /// Inserts `value` in this entry slot.
    pub fn insert(self, value: T) {
        let VacantEntry(state, salt) = self;
        let slot = match state {
            VacantState::NeedSlot(slab, base) => slab.get_slot_mut(base),
            VacantState::EmptySlot(slot) => slot,
        };
        assert!(slot.replace(Slot { salt, value }).is_none(), "violated VacantEntry invariant");
    }
}

/// An occupied entry in a [`PortSlab`].
#[derive(Debug)]
pub struct OccupiedEntry<'a, T>(&'a mut Option<Slot<T>>);

impl<'a, T> OccupiedEntry<'a, T> {
    /// Gets a reference to the stored value.
    pub fn get(&self) -> &T {
        let OccupiedEntry(slot) = self;
        // OccupiedEntry is a witness to the slot being filled.
        &slot.as_ref().unwrap().value
    }

    /// Gets a mutable reference to the stored value.
    pub fn get_mut(&mut self) -> &mut T {
        let OccupiedEntry(slot) = self;
        // OccupiedEntry is a witness to the slot being filled.
        &mut slot.as_mut().unwrap().value
    }

    /// Removes the value from the slab.
    pub fn remove(self) -> T {
        let OccupiedEntry(slot) = self;
        // OccupiedEntry is a witness to the slot being filled.
        slot.take().unwrap().value
    }
}

/// A mismatched salt entry in a [`PortSlab`].
#[derive(Debug)]
pub struct SaltMismatchEntry<'a, T>(&'a mut Option<Slot<T>>);

impl<'a, T> SaltMismatchEntry<'a, T> {
    /// Removes the mismatched entry from the slab.
    pub fn remove(self) -> T {
        let SaltMismatchEntry(slot) = self;
        // SaltMismatch is a witness to the slot being filled.
        slot.take().unwrap().value
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    const PORT_A: Port = Port { base: 0, salt: 1 };
    const PORT_A_GEN_2: Port = Port { salt: 2, ..PORT_A };
    const PORT_B: Port = Port { base: 1, salt: 1 };

    #[test]
    fn insert_new_entry() {
        let mut slab = PortSlab::new();
        assert_eq!(slab.insert(PORT_A, 0), None);
        assert_eq!(slab.get(&PORT_A), Some(&0));
    }

    #[test]
    fn insert_replaces() {
        let mut slab = PortSlab::new();
        assert_eq!(slab.insert(PORT_A, 0), None);
        assert_eq!(slab.insert(PORT_A, 1), Some(0));
        assert_eq!(slab.get(&PORT_A), Some(&1));
    }

    #[test]
    fn insert_replaces_even_on_salt_mismatch() {
        let mut slab = PortSlab::new();
        assert_eq!(slab.insert(PORT_A, 0), None);
        assert_eq!(slab.insert(PORT_A_GEN_2, 1), Some(0));
        assert_eq!(slab.get(&PORT_A), None);
        assert_eq!(slab.get(&PORT_A_GEN_2), Some(&1));
    }

    #[test]
    fn remove_nonexisting() {
        let mut slab = PortSlab::<u32>::new();
        assert_eq!(slab.remove(&PORT_A), RemoveOutcome::PortNotPresent);
    }

    #[test]
    fn remove_matching_salt() {
        let mut slab = PortSlab::new();
        assert_eq!(slab.insert(PORT_A, 0), None);
        assert_eq!(slab.remove(&PORT_A), RemoveOutcome::Removed(0));
        assert_eq!(slab.get(&PORT_A), None);
    }

    #[test]
    fn remove_salt_mismatch() {
        let mut slab = PortSlab::new();
        assert_eq!(slab.insert(PORT_A, 0), None);
        assert_eq!(slab.remove(&PORT_A_GEN_2), RemoveOutcome::SaltMismatch(PORT_A.salt));
        assert_eq!(slab.get(&PORT_A), Some(&0));
    }

    #[test]
    fn get_mut() {
        let mut slab = PortSlab::new();
        assert_eq!(slab.get_mut(&PORT_A), None);
        assert_eq!(slab.insert(PORT_A, 0), None);
        assert_eq!(slab.insert(PORT_B, 1), None);
        let a = slab.get_mut(&PORT_A).unwrap();
        assert_eq!(*a, 0);
        *a = 3;
        assert_eq!(slab.get_mut(&PORT_A_GEN_2), None);
        assert_eq!(slab.get_mut(&PORT_A), Some(&mut 3));
    }

    #[test]
    fn entry_vacant_no_slot() {
        let mut slab = PortSlab::new();
        let vacant = assert_matches!(slab.entry(PORT_A), Entry::Vacant(v) => v);
        vacant.insert(1);
        assert_eq!(slab.get(&PORT_A), Some(&1));
    }

    #[test]
    fn entry_vacant_existing_slot() {
        let mut slab = PortSlab::new();
        assert_eq!(slab.insert(PORT_A, 0), None);
        assert_eq!(slab.remove(&PORT_A), RemoveOutcome::Removed(0));
        let vacant = assert_matches!(slab.entry(PORT_A), Entry::Vacant(v) => v);
        vacant.insert(1);
        assert_eq!(slab.get(&PORT_A), Some(&1));
    }

    #[test]
    fn entry_occupied_get() {
        let mut slab = PortSlab::new();
        assert_eq!(slab.insert(PORT_A, 2), None);
        let mut occupied = assert_matches!(slab.entry(PORT_A), Entry::Occupied(o) => o);
        assert_eq!(occupied.get(), &2);
        assert_eq!(occupied.get_mut(), &mut 2);
    }

    #[test]
    fn entry_occupied_remove() {
        let mut slab = PortSlab::new();
        assert_eq!(slab.insert(PORT_A, 2), None);
        let occupied = assert_matches!(slab.entry(PORT_A), Entry::Occupied(o) => o);
        assert_eq!(occupied.remove(), 2);
        assert_eq!(slab.get(&PORT_A), None);
    }

    #[test]
    fn entry_mismatch() {
        let mut slab = PortSlab::new();
        assert_eq!(slab.insert(PORT_A, 2), None);
        let mismatch = assert_matches!(slab.entry(PORT_A_GEN_2), Entry::SaltMismatch(m) => m);
        assert_eq!(mismatch.remove(), 2);
        assert_eq!(slab.get(&PORT_A), None);
        assert_eq!(slab.get(&PORT_A_GEN_2), None);
    }

    #[test]
    fn underlying_vec_only_grows() {
        let mut slab = PortSlab::new();
        let high_port = Port { base: 4, salt: 0 };
        let low_port = Port { base: 0, salt: 0 };
        assert_eq!(slab.slots.len(), 0, "{:?}", slab.slots);
        assert_eq!(slab.insert(high_port, 0), None);
        assert_eq!(slab.slots.len(), usize::from(high_port.base + 1), "{:?}", slab.slots);
        assert_eq!(slab.remove(&high_port), RemoveOutcome::Removed(0));
        assert_eq!(slab.slots.len(), usize::from(high_port.base + 1), "{:?}", slab.slots);
        assert_eq!(slab.insert(low_port, 1), None);
    }
}
