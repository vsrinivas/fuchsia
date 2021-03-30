//! A simple object arena.
//!
//! `Arena<T>` is basically just a `Vec<Option<T>>`, which allows you to:
//!
//! * Insert an object (reuse an existing [`None`] element, or append to the end).
//! * Remove object at a specified index.
//! * Access object at a specified index.
//!
//! # Examples
//!
//! Some data structures built using `Arena<T>`:
//!
//! * [Doubly linked list](https://github.com/smol-rs/vec-arena/blob/master/examples/linked-list.rs)
//! * [Splay tree](https://github.com/smol-rs/vec-arena/blob/master/examples/splay-tree.rs)

#![no_std]
#![forbid(unsafe_code)]
#![warn(missing_docs, missing_debug_implementations, rust_2018_idioms)]

extern crate alloc;

use alloc::fmt;
use alloc::vec;
use alloc::vec::Vec;
use core::iter;
use core::mem;
use core::ops::{Index, IndexMut};
use core::slice;

/// A slot, which is either vacant or occupied.
///
/// Vacant slots in arena are linked together into a singly linked list. This allows the arena to
/// efficiently find a vacant slot before inserting a new object, or reclaiming a slot after
/// removing an object.
#[derive(Clone)]
enum Slot<T> {
    /// Vacant slot, containing index to the next slot in the linked list.
    Vacant(usize),

    /// Occupied slot, containing a value.
    Occupied(T),
}

impl<T> Slot<T> {
    /// Returns `true` if the slot is vacant.
    fn is_occupied(&self) -> bool {
        match self {
            Slot::Vacant(_) => false,
            Slot::Occupied(_) => true,
        }
    }
}

/// An object arena.
///
/// `Arena<T>` holds an array of slots for storing objects.
/// Every slot is always in one of two states: occupied or vacant.
///
/// Essentially, this is equivalent to `Vec<Option<T>>`.
///
/// # Insert and remove
///
/// When inserting a new object into arena, a vacant slot is found and then the object is placed
/// into the slot. If there are no vacant slots, the array is reallocated with bigger capacity.
/// The cost of insertion is amortized `O(1)`.
///
/// When removing an object, the slot containing it is marked as vacant and the object is returned.
/// The cost of removal is `O(1)`.
///
/// ```
/// use vec_arena::Arena;
///
/// let mut arena = Arena::new();
/// let a = arena.insert(10);
/// let b = arena.insert(20);
///
/// assert_eq!(a, 0); // 10 was placed at index 0
/// assert_eq!(b, 1); // 20 was placed at index 1
///
/// assert_eq!(arena.remove(a), Some(10));
/// assert_eq!(arena.get(a), None); // slot at index 0 is now vacant
///
/// assert_eq!(arena.insert(30), 0); // slot at index 0 is reused
/// ```
///
/// # Indexing
///
/// You can also access objects in an arena by index, just like you would in a [`Vec`].
/// However, accessing a vacant slot by index or using an out-of-bounds index will result in panic.
///
/// ```
/// use vec_arena::Arena;
///
/// let mut arena = Arena::new();
/// let a = arena.insert(10);
/// let b = arena.insert(20);
///
/// assert_eq!(arena[a], 10);
/// assert_eq!(arena[b], 20);
///
/// arena[a] += arena[b];
/// assert_eq!(arena[a], 30);
/// ```
///
/// To access slots without fear of panicking, use [`get()`][`Arena::get()`] and
/// [`get_mut()`][`Arena::get_mut()`], which return [`Option`]s.
pub struct Arena<T> {
    /// Slots in which objects are stored.
    slots: Vec<Slot<T>>,

    /// Number of occupied slots in the arena.
    len: usize,

    /// Index of the first vacant slot in the linked list.
    head: usize,
}

impl<T> Arena<T> {
    /// Constructs a new, empty arena.
    ///
    /// The arena will not allocate until objects are inserted into it.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena: Arena<i32> = Arena::new();
    /// ```
    #[inline]
    pub fn new() -> Self {
        Arena {
            slots: Vec::new(),
            len: 0,
            head: !0,
        }
    }

    /// Constructs a new, empty arena with the specified capacity (number of slots).
    ///
    /// The arena will be able to hold exactly `cap` objects without reallocating.
    /// If `cap` is 0, the arena will not allocate.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::with_capacity(10);
    ///
    /// assert_eq!(arena.len(), 0);
    /// assert_eq!(arena.capacity(), 10);
    ///
    /// // These inserts are done without reallocating...
    /// for i in 0..10 {
    ///     arena.insert(i);
    /// }
    /// assert_eq!(arena.capacity(), 10);
    ///
    /// // ... but this one will reallocate.
    /// arena.insert(11);
    /// assert!(arena.capacity() > 10);
    /// ```
    #[inline]
    pub fn with_capacity(cap: usize) -> Self {
        Arena {
            slots: Vec::with_capacity(cap),
            len: 0,
            head: !0,
        }
    }

    /// Returns the number of slots in the arena.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let arena: Arena<i32> = Arena::with_capacity(10);
    /// assert_eq!(arena.capacity(), 10);
    /// ```
    #[inline]
    pub fn capacity(&self) -> usize {
        self.slots.capacity()
    }

    /// Returns the number of occupied slots in the arena.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// assert_eq!(arena.len(), 0);
    ///
    /// for i in 0..10 {
    ///     arena.insert(());
    ///     assert_eq!(arena.len(), i + 1);
    /// }
    /// ```
    #[inline]
    pub fn len(&self) -> usize {
        self.len
    }

    /// Returns `true` if all slots are vacant.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// assert!(arena.is_empty());
    ///
    /// arena.insert(1);
    /// assert!(!arena.is_empty());
    /// ```
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Returns the index of the slot that next [`insert`][`Arena::insert()`] will use if no
    /// mutating calls take place in between.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    ///
    /// let a = arena.next_vacant();
    /// let b = arena.insert(1);
    /// assert_eq!(a, b);
    /// let c = arena.next_vacant();
    /// let d = arena.insert(2);
    /// assert_eq!(c, d);
    /// ```
    #[inline]
    pub fn next_vacant(&self) -> usize {
        if self.head == !0 {
            self.len
        } else {
            self.head
        }
    }

    /// Inserts an object into the arena and returns the slot index it was stored in.
    ///
    /// The arena will reallocate if it's full.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    ///
    /// let a = arena.insert(1);
    /// let b = arena.insert(2);
    /// assert!(a != b);
    /// ```
    #[inline]
    pub fn insert(&mut self, object: T) -> usize {
        self.len += 1;

        if self.head == !0 {
            self.slots.push(Slot::Occupied(object));
            self.len - 1
        } else {
            let index = self.head;
            match self.slots[index] {
                Slot::Vacant(next) => {
                    self.head = next;
                    self.slots[index] = Slot::Occupied(object);
                }
                Slot::Occupied(_) => unreachable!(),
            }
            index
        }
    }

    /// Removes the object stored at `index` from the arena and returns it.
    ///
    /// If the slot is vacant or `index` is out of bounds, [`None`] will be returned.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// let a = arena.insert("hello");
    ///
    /// assert_eq!(arena.len(), 1);
    /// assert_eq!(arena.remove(a), Some("hello"));
    ///
    /// assert_eq!(arena.len(), 0);
    /// assert_eq!(arena.remove(a), None);
    /// ```
    #[inline]
    pub fn remove(&mut self, index: usize) -> Option<T> {
        match self.slots.get_mut(index) {
            None => None,
            Some(&mut Slot::Vacant(_)) => None,
            Some(slot @ &mut Slot::Occupied(_)) => {
                if let Slot::Occupied(object) = mem::replace(slot, Slot::Vacant(self.head)) {
                    self.head = index;
                    self.len -= 1;
                    Some(object)
                } else {
                    unreachable!();
                }
            }
        }
    }

    /// Retains objects for which the closure returns `true`.
    ///
    /// All other objects will be removed from the arena.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    ///
    /// let a = arena.insert(0);
    /// let b = arena.insert(1);
    /// let c = arena.insert(2);
    ///
    /// arena.retain(|k, v| k == a || *v == 1);
    ///
    /// assert!(arena.get(a).is_some());
    /// assert!(arena.get(b).is_some());
    /// assert!(arena.get(c).is_none());
    /// ```
    pub fn retain<F>(&mut self, mut f: F)
    where
        F: FnMut(usize, &mut T) -> bool,
    {
        for i in 0..self.slots.len() {
            if let Slot::Occupied(v) = &mut self.slots[i] {
                if !f(i, v) {
                    self.remove(i);
                }
            }
        }
    }

    /// Clears the arena, removing and dropping all objects it holds.
    ///
    /// Keeps the allocated memory for reuse.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// for i in 0..10 {
    ///     arena.insert(i);
    /// }
    ///
    /// assert_eq!(arena.len(), 10);
    /// arena.clear();
    /// assert_eq!(arena.len(), 0);
    /// ```
    #[inline]
    pub fn clear(&mut self) {
        self.slots.clear();
        self.len = 0;
        self.head = !0;
    }

    /// Returns a reference to the object stored at `index`.
    ///
    /// If the slot is vacant or `index` is out of bounds, [`None`] will be returned.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// let index = arena.insert("hello");
    ///
    /// assert_eq!(arena.get(index), Some(&"hello"));
    /// arena.remove(index);
    /// assert_eq!(arena.get(index), None);
    /// ```
    #[inline]
    pub fn get(&self, index: usize) -> Option<&T> {
        match self.slots.get(index) {
            None => None,
            Some(&Slot::Vacant(_)) => None,
            Some(&Slot::Occupied(ref object)) => Some(object),
        }
    }

    /// Returns a mutable reference to the object stored at `index`.
    ///
    /// If the slot is vacant or `index` is out of bounds, [`None`] will be returned.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// let index = arena.insert(7);
    ///
    /// assert_eq!(arena.get_mut(index), Some(&mut 7));
    /// *arena.get_mut(index).unwrap() *= 10;
    /// assert_eq!(arena.get_mut(index), Some(&mut 70));
    /// ```
    #[inline]
    pub fn get_mut(&mut self, index: usize) -> Option<&mut T> {
        match self.slots.get_mut(index) {
            None => None,
            Some(&mut Slot::Vacant(_)) => None,
            Some(&mut Slot::Occupied(ref mut object)) => Some(object),
        }
    }

    /// Swaps two objects in the arena.
    ///
    /// The two indices are `a` and `b`.
    ///
    /// # Panics
    ///
    /// Panics if any of the indices is out of bounds or any of the slots is vacant.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// let a = arena.insert(7);
    /// let b = arena.insert(8);
    ///
    /// arena.swap(a, b);
    /// assert_eq!(arena.get(a), Some(&8));
    /// assert_eq!(arena.get(b), Some(&7));
    /// ```
    #[inline]
    pub fn swap(&mut self, a: usize, b: usize) {
        assert!(self.slots[a].is_occupied(), "invalid object ID");
        assert!(self.slots[b].is_occupied(), "invalid object ID");

        if a != b {
            let (a, b) = (a.min(b), a.max(b));
            let (l, r) = self.slots.split_at_mut(b);
            mem::swap(&mut l[a], &mut r[0]);
        }
    }

    /// Reserves capacity for at least `additional` more objects to be inserted.
    ///
    /// The arena may reserve more space to avoid frequent reallocations.
    ///
    /// # Panics
    ///
    /// Panics if the new capacity overflows `usize`.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// arena.insert("hello");
    ///
    /// arena.reserve(10);
    /// assert!(arena.capacity() >= 11);
    /// ```
    pub fn reserve(&mut self, additional: usize) {
        let vacant = self.slots.len() - self.len;
        if additional > vacant {
            self.slots.reserve(additional - vacant);
        }
    }

    /// Reserves the minimum capacity for exactly `additional` more objects to be inserted.
    ///
    /// Note that the allocator may give the arena more space than it requests.
    ///
    /// # Panics
    ///
    /// Panics if the new capacity overflows `usize`.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// arena.insert("hello");
    ///
    /// arena.reserve_exact(10);
    /// assert!(arena.capacity() >= 11);
    /// ```
    pub fn reserve_exact(&mut self, additional: usize) {
        let vacant = self.slots.len() - self.len;
        if additional > vacant {
            self.slots.reserve_exact(additional - vacant);
        }
    }

    /// Returns an iterator over occupied slots.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// arena.insert(1);
    /// arena.insert(2);
    /// arena.insert(4);
    ///
    /// let mut iterator = arena.iter();
    /// assert_eq!(iterator.next(), Some((0, &1)));
    /// assert_eq!(iterator.next(), Some((1, &2)));
    /// assert_eq!(iterator.next(), Some((2, &4)));
    /// ```
    #[inline]
    pub fn iter(&self) -> Iter<'_, T> {
        Iter {
            slots: self.slots.iter().enumerate(),
        }
    }

    /// Returns an iterator that returns mutable references to objects.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::new();
    /// arena.insert("zero".to_string());
    /// arena.insert("one".to_string());
    /// arena.insert("two".to_string());
    ///
    /// for (index, object) in arena.iter_mut() {
    ///     *object = index.to_string() + " " + object;
    /// }
    ///
    /// let mut iterator = arena.iter();
    /// assert_eq!(iterator.next(), Some((0, &"0 zero".to_string())));
    /// assert_eq!(iterator.next(), Some((1, &"1 one".to_string())));
    /// assert_eq!(iterator.next(), Some((2, &"2 two".to_string())));
    /// ```
    #[inline]
    pub fn iter_mut(&mut self) -> IterMut<'_, T> {
        IterMut {
            slots: self.slots.iter_mut().enumerate(),
        }
    }

    /// Shrinks the capacity of the arena as much as possible.
    ///
    /// It will drop down as close as possible to the length but the allocator may still inform
    /// the arena that there is space for a few more elements.
    ///
    /// # Examples
    ///
    /// ```
    /// use vec_arena::Arena;
    ///
    /// let mut arena = Arena::with_capacity(10);
    /// arena.insert("first".to_string());
    /// arena.insert("second".to_string());
    /// arena.insert("third".to_string());
    /// assert_eq!(arena.capacity(), 10);
    /// arena.shrink_to_fit();
    /// assert!(arena.capacity() >= 3);
    /// ```
    pub fn shrink_to_fit(&mut self) {
        self.slots.shrink_to_fit();
    }
}

impl<T> fmt::Debug for Arena<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Arena {{ ... }}")
    }
}

impl<T> Index<usize> for Arena<T> {
    type Output = T;

    #[inline]
    fn index(&self, index: usize) -> &T {
        self.get(index).expect("vacant slot at `index`")
    }
}

impl<T> IndexMut<usize> for Arena<T> {
    #[inline]
    fn index_mut(&mut self, index: usize) -> &mut T {
        self.get_mut(index).expect("vacant slot at `index`")
    }
}

impl<T> Default for Arena<T> {
    fn default() -> Self {
        Arena::new()
    }
}

impl<T: Clone> Clone for Arena<T> {
    fn clone(&self) -> Self {
        Arena {
            slots: self.slots.clone(),
            len: self.len,
            head: self.head,
        }
    }
}

/// An iterator over the occupied slots in an [`Arena`].
pub struct IntoIter<T> {
    slots: iter::Enumerate<vec::IntoIter<Slot<T>>>,
}

impl<T> Iterator for IntoIter<T> {
    type Item = (usize, T);

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        while let Some((index, slot)) = self.slots.next() {
            if let Slot::Occupied(object) = slot {
                return Some((index, object));
            }
        }
        None
    }
}

impl<T> IntoIterator for Arena<T> {
    type Item = (usize, T);
    type IntoIter = IntoIter<T>;

    #[inline]
    fn into_iter(self) -> Self::IntoIter {
        IntoIter {
            slots: self.slots.into_iter().enumerate(),
        }
    }
}

impl<T> iter::FromIterator<T> for Arena<T> {
    fn from_iter<U: IntoIterator<Item = T>>(iter: U) -> Arena<T> {
        let iter = iter.into_iter();
        let mut arena = Arena::with_capacity(iter.size_hint().0);
        for i in iter {
            arena.insert(i);
        }
        arena
    }
}

impl<T> fmt::Debug for IntoIter<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "IntoIter {{ ... }}")
    }
}

/// An iterator over references to the occupied slots in an [`Arena`].
pub struct Iter<'a, T> {
    slots: iter::Enumerate<slice::Iter<'a, Slot<T>>>,
}

impl<'a, T> Iterator for Iter<'a, T> {
    type Item = (usize, &'a T);

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        while let Some((index, slot)) = self.slots.next() {
            if let Slot::Occupied(ref object) = *slot {
                return Some((index, object));
            }
        }
        None
    }
}

impl<'a, T> IntoIterator for &'a Arena<T> {
    type Item = (usize, &'a T);
    type IntoIter = Iter<'a, T>;

    #[inline]
    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<'a, T> fmt::Debug for Iter<'a, T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Iter {{ ... }}")
    }
}

/// An iterator over mutable references to the occupied slots in a `Arena`.
pub struct IterMut<'a, T> {
    slots: iter::Enumerate<slice::IterMut<'a, Slot<T>>>,
}

impl<'a, T> Iterator for IterMut<'a, T> {
    type Item = (usize, &'a mut T);

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        while let Some((index, slot)) = self.slots.next() {
            if let Slot::Occupied(ref mut object) = *slot {
                return Some((index, object));
            }
        }
        None
    }
}

impl<'a, T> IntoIterator for &'a mut Arena<T> {
    type Item = (usize, &'a mut T);
    type IntoIter = IterMut<'a, T>;

    #[inline]
    fn into_iter(self) -> Self::IntoIter {
        self.iter_mut()
    }
}

impl<'a, T> fmt::Debug for IterMut<'a, T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "IterMut {{ ... }}")
    }
}
