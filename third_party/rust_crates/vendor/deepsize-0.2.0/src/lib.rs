#![forbid(missing_docs)]
#![cfg_attr(not(feature = "std"), no_std)]

//! A utility for recursively measuring the size of an object
//!
//! This contains the [`DeepSizeOf`](DeepSizeOf) trait, and re-exports
//! the `DeepSizeOf` derive macro from [`deepsize_derive`](https://docs.rs/deepsize_derive)
//!
//! ```rust
//! use deepsize::DeepSizeOf;
//!
//! #[derive(DeepSizeOf)]
//! struct Test {
//!     a: u32,
//!     b: Box<u8>,
//! }
//!
//! let object = Test {
//!     a: 15,
//!     b: Box::new(255),
//! };
//!
//! // The stack size of the struct:
//! //    The size of a u32 (4)
//! //    4 bytes padding (64 bit only)
//! //    The stack size of the Box (a usize pointer, 32 or 64 bits: 4 or 8 bytes)
//! // + the size of a u8 (1), the Box's heap storage
//! #[cfg(target_pointer_width = "64")]
//! assert_eq!(object.deep_size_of(), 17);
//! #[cfg(target_pointer_width = "32")]
//! assert_eq!(object.deep_size_of(), 9);
//! ```
//!

extern crate alloc;
extern crate core;

#[cfg(feature = "derive")]
extern crate self as deepsize;
#[cfg(feature = "derive")]
pub use deepsize_derive::*;

use core::mem::{size_of, size_of_val};

#[cfg(test)]
mod test;

mod default_impls;
mod external_impls;

/// A trait for measuring the size of an object and its children
///
/// In many cases this is just `std::mem::size_of::<T>()`, but if
/// the struct contains a `Vec`, `String`, `Box`, or other allocated object or
/// reference, then it is the size of the struct, plus the size of the contents
/// of the object.
pub trait DeepSizeOf {
    /// Returns an estimation of a total size of memory owned by the
    /// object, including heap-managed storage.
    ///
    /// This is an estimation and not a precise result because it
    /// doesn't account for allocator's overhead.
    ///
    /// ```rust
    /// use deepsize::DeepSizeOf;
    ///
    /// let mut map: Vec<(Box<u32>, String)> = Vec::new();
    ///
    /// map.push((Box::new(42u32), String::from("Hello World")));
    /// map.push((Box::new(20u32), String::from("Something")));
    /// map.push((Box::new(0u32),  String::from("A string")));
    /// map.push((Box::new(255u32), String::from("Dynamically Allocated!")));
    ///
    /// assert_eq!(map.deep_size_of(),
    ///     std::mem::size_of::<Vec<(Box<u32>, String)>>() +
    ///     4 * std::mem::size_of::<(Box<u32>, String)>() +
    ///     4 * std::mem::size_of::<u32>() +
    ///     11 + 9 + 8 + 22
    /// );
    /// ```
    fn deep_size_of(&self) -> usize {
        size_of_val(self) + self.deep_size_of_children(&mut Context::new())
    }

    /// Returns an estimation of the heap-managed storage of this object.
    /// This does not include the size of the object itself.
    ///
    /// This is an estimation and not a precise result, because it
    /// doesn't account for allocator's overhead.
    ///
    /// This is an internal function (this shouldn't be called directly),
    /// and requires a [`Context`](Context) to track visited references.
    /// Implementations of this function should only call `deep_size_of_children`,
    /// and not `deep_size_of` so that they reference tracking is not reset.
    ///
    /// In all other cases, `deep_size_of` should be called instead of this function.
    ///
    /// If a struct and all of its children do not allocate or have references,
    /// this method should return `0`, as it cannot have any heap allocated
    /// children.  There is a shortcut macro for this implementation, [`known_size_of`](known_size_of),
    /// used like `known_deep_size!(0, (), u32, u64);` which generates the impls.
    ///
    /// The most common way to use this method, and how the derive works,
    /// is to call this method on each of the structs members and sum the
    /// results, which works as long as all members of the struct implement
    /// `DeepSizeOf`.
    ///
    /// To implement this for a collection type, you should sum the deep sizes of
    /// the items of the collection and then add the size of the allocation of the
    /// collection itself.  This can become much more complicated if the collection
    /// has multiple separate allocations.
    ///
    /// Here is an example from the implementation of `DeepSizeOf` for `Vec<T>`
    /// ```rust, ignore
    /// # use deepsize::{DeepSizeOf, Context};
    /// impl<T> DeepSizeOf for std::vec::Vec<T> where T: DeepSizeOf {
    ///     fn deep_size_of_children(&self, context: &mut Context) -> usize {
    ///         // Size of heap allocations for each child
    ///         self.iter().map(|child| child.deep_size_of_children(context)).sum()
    ///          + self.capacity() * std::mem::size_of::<T>()  // Size of Vec's heap allocation
    ///     }
    /// }
    /// ```
    fn deep_size_of_children(&self, context: &mut Context) -> usize;
}

#[cfg(not(feature = "std"))]
use alloc::collections::BTreeSet as GenericSet;
#[cfg(feature = "std")]
use std::collections::HashSet as GenericSet;

/// The context of which references have already been seen.
/// This should only be used in the implementation of the
/// `deep_size_of_children` function, and (eventually, when this
/// reaches 0.2) will not be able to be constructed, and only obtained
/// from inside the function.
///
/// Keeps track of the [`Arc`](std::sync::Arc)s, [`Rc`](std::rc::Rc)s, and references
/// that have been visited, so that [`Arc`](std::sync::Arc)s and other references
/// aren't double counted.
///
/// Currently this counts each reference once, although there are arguments for
/// only counting owned data and ignoring partial ownership, or for counting
/// partial references such as Arc as its size divided by the strong reference count.
///
/// [Github issue discussion here](https://github.com/dtolnay/request-for-implementation/issues/22)
///
/// This must be passed between `deep_size_of_children` calls when
/// recursing, so that references are not counted multiple timse.
#[derive(Debug)]
pub struct Context {
    /// A set of all [`Arc`](std::sync::Arc)s that have already been counted
    arcs: GenericSet<usize>,
    /// A set of all [`Rc`](std::sync::Arc)s that have already been counted
    rcs: GenericSet<usize>,
}

impl Context {
    /// Creates a new empty context for use in the `deep_size` functions
    fn new() -> Self {
        Self {
            arcs: GenericSet::new(),
            rcs: GenericSet::new(),
        }
    }

    /// Adds an [`Arc`](std::sync::Arc) to the list of visited [`Arc`](std::sync::Arc)s
    fn add_arc<T: ?Sized>(&mut self, arc: &alloc::sync::Arc<T>) {
        self.arcs.insert(&**arc as *const T as *const u8 as usize);
    }
    /// Checks if an [`Arc`](std::sync::Arc) is in the list visited [`Arc`](std::sync::Arc)s
    fn contains_arc<T: ?Sized>(&self, arc: &alloc::sync::Arc<T>) -> bool {
        self.arcs
            .contains(&(&**arc as *const T as *const u8 as usize))
    }

    /// Adds an [`Rc`](std::rc::Rc) to the list of visited [`Rc`](std::rc::Rc)s
    fn add_rc<T: ?Sized>(&mut self, rc: &alloc::rc::Rc<T>) {
        self.rcs.insert(&**rc as *const T as *const u8 as usize);
    }
    /// Checks if an [`Rc`](std::rc::Rc) is in the list visited [`Rc`](std::rc::Rc)s
    /// Adds an [`Rc`](std::rc::Rc) to the list of visited [`Rc`](std::rc::Rc)s
    fn contains_rc<T: ?Sized>(&self, rc: &alloc::rc::Rc<T>) -> bool {
        self.rcs
            .contains(&(&**rc as *const T as *const u8 as usize))
    }
}

impl<T> DeepSizeOf for alloc::vec::Vec<T>
where
    T: DeepSizeOf,
{
    /// Sums the size of each child object, and then adds the size of
    /// the unused capacity.
    ///
    /// ```rust
    /// use deepsize::DeepSizeOf;
    ///
    /// let mut vec: Vec<u8> = vec![];
    /// for i in 0..13 {
    ///     vec.push(i);
    /// }
    ///
    /// // The capacity (16) plus three usizes (len, cap, pointer)
    /// assert_eq!(vec.deep_size_of(), 16 + 24);
    /// ```
    /// With allocated objects:
    /// ```rust
    /// use deepsize::DeepSizeOf;
    ///
    /// let mut vec: Vec<Box<u64>> = vec![];
    /// for i in 0..13 {
    ///     vec.push(Box::new(i));
    /// }
    ///
    /// // The capacity (16?) * size (8) plus three usizes (len, cap, pointer)
    /// // and length (13) * the allocated size of each object
    /// assert_eq!(vec.deep_size_of(), 24 + vec.capacity() * 8 + 13 * 8);
    /// ```
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        self.iter()
            .map(|child| child.deep_size_of_children(context))
            .sum::<usize>()
            + self.capacity() * size_of::<T>()
        // Size of unused capacity
    }
}

impl<T> DeepSizeOf for alloc::collections::VecDeque<T>
where
    T: DeepSizeOf,
{
    /// Sums the size of each child object, and then adds the size of
    /// the unused capacity.
    ///
    /// ```rust
    /// use deepsize::DeepSizeOf;
    /// use std::collections::VecDeque;
    ///
    /// let mut vec: VecDeque<u8> = VecDeque::new();
    /// for i in 0..12 {
    ///     vec.push_back(i);
    /// }
    /// vec.push_front(13);
    ///
    /// // The capacity (15?) plus four usizes (start, end, cap, pointer)
    /// assert_eq!(vec.deep_size_of(), vec.capacity() * 1 + 32);
    /// ```
    /// With allocated objects:
    /// ```rust
    /// use deepsize::DeepSizeOf;
    /// use std::collections::VecDeque;
    ///
    /// let mut vec: VecDeque<Box<u64>> = VecDeque::new();
    /// for i in 0..12 {
    ///     vec.push_back(Box::new(i));
    /// }
    /// vec.push_front(Box::new(13));
    ///
    /// // The capacity (15?) * size (8) plus four usizes (start, end, cap, pointer)
    /// // and length (13) * the allocated size of each object
    /// assert_eq!(vec.deep_size_of(), 32 + vec.capacity() * 8 + 13 * 8);
    /// ```
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        // Deep size of children
        self.iter()
            .map(|child| child.deep_size_of_children(context))
            .sum::<usize>()
            + self.capacity() * size_of::<T>() // Size of Vec's heap allocation
    }
}

impl<T> DeepSizeOf for alloc::collections::LinkedList<T>
where
    T: DeepSizeOf,
{
    /// Sums the size of each child object, assuming the overhead of
    /// each node is 2 usize (next, prev)
    ///
    /// ```rust
    /// use deepsize::DeepSizeOf;
    /// use std::collections::LinkedList;
    ///
    /// let mut list: LinkedList<u8> = LinkedList::new();
    /// for i in 0..12 {
    ///     list.push_back(i);
    /// }
    /// list.push_front(13);
    ///
    /// assert_eq!(list.deep_size_of(), std::mem::size_of::<LinkedList<u8>>()
    ///                                + 13 * 1 + 13 * 2 * 8);
    /// ```
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        self.iter().fold(0, |sum, child| {
            sum + size_of_val(child) + child.deep_size_of_children(context) + size_of::<usize>() * 2
            // overhead of each node
        })
    }
}

#[cfg(feature = "std")]
impl<K, V, S> DeepSizeOf for std::collections::HashMap<K, V, S>
where
    K: DeepSizeOf + Eq + std::hash::Hash,
    V: DeepSizeOf,
    S: std::hash::BuildHasher,
{
    // How much more overhead is there to a hashmap? The docs say it is
    // essentially just a Vec<Option<(u64, K, V)>>;
    // For the old implementation of HashMap:
    // fn deep_size_of_children(&self, context: &mut Context) -> usize {
    //     self.iter().fold(0, |sum, (key, val)| {
    //         sum + key.deep_size_of_children(context) + val.deep_size_of_children(context)
    //     }) + self.capacity() * size_of::<Option<(u64, K, V)>>()
    // }
    // For the hashbrown implementation of HashMap:
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        self.iter().fold(0, |sum, (key, val)| {
            sum + key.deep_size_of_children(context) + val.deep_size_of_children(context)
        }) + self.capacity() * size_of::<(K, V)>()
        // Buckets would be the more correct value, but there isn't
        // an API for accessing that with hashbrown.
        // I believe that hashbrown's HashTable is represented as
        // an array of (K, V), with control bytes at the start/end
        // that mark used/uninitialized buckets (?)
    }
}

#[cfg(feature = "std")]
impl<K, S> DeepSizeOf for std::collections::HashSet<K, S>
where
    K: DeepSizeOf + Eq + std::hash::Hash,
    S: std::hash::BuildHasher,
{
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        // self.iter()
        //     .fold(0, |sum, item| sum + item.deep_size_of_children(context))
        //     + self.capacity() * size_of::<Option<(u64, K, ())>>()
        self.iter()
            .fold(0, |sum, key| sum + key.deep_size_of_children(context))
            + self.capacity() * size_of::<K>()
    }
}

// A btree node has 2*B - 1 (K,V) pairs and (usize, u16, u16)
// overhead, and an internal btree node additionally has 2*B
// `usize` overhead.
// A node can contain between B - 1 and 2*B - 1 elements, so
// we assume it has the midpoint 3/2*B - 1.

// Constants from rust's source:
// https://doc.rust-lang.org/src/alloc/collections/btree/node.rs.html#43-45

const BTREE_B: usize = 6;
const BTREE_MIN: usize = 2 * BTREE_B - 1;
const BTREE_MAX: usize = BTREE_B - 1;

#[cfg(feature = "std")]
impl<K: Ord + DeepSizeOf, V: DeepSizeOf> DeepSizeOf for std::collections::BTreeMap<K, V> {
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        let element_size = self.iter().fold(0, |sum, (k, v)| {
            sum + k.deep_size_of_children(context) + v.deep_size_of_children(context)
        });
        let overhead = size_of::<(usize, u16, u16, [(K, V); BTREE_MAX], [usize; BTREE_B])>();
        element_size + self.len() * overhead * 2 / (BTREE_MAX + BTREE_MIN)
    }
}

#[cfg(feature = "std")]
impl<K: Ord + DeepSizeOf> DeepSizeOf for std::collections::BTreeSet<K> {
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        let element_size = self
            .iter()
            .fold(0, |sum, item| sum + item.deep_size_of_children(context));
        let overhead = size_of::<(usize, u16, u16, [K; BTREE_MAX], [usize; BTREE_B])>();
        element_size + self.len() * overhead * 2 / (BTREE_MAX + BTREE_MIN)
    }
}

impl<T> DeepSizeOf for alloc::boxed::Box<T>
where
    T: DeepSizeOf + ?Sized,
{
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        let val: &T = &*self;
        size_of_val(val) + val.deep_size_of_children(context)
    }
}

impl<T> DeepSizeOf for alloc::sync::Arc<T>
where
    T: DeepSizeOf + ?Sized,
{
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        if context.contains_arc(self) {
            0
        } else {
            context.add_arc(self);
            let val: &T = &*self;
            // Size of the Arc, size of the value, size of the allocations of the value
            size_of_val(val) + val.deep_size_of_children(context)
        }
    }
}

impl<T> DeepSizeOf for alloc::rc::Rc<T>
where
    T: DeepSizeOf + ?Sized,
{
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        if context.contains_rc(self) {
            0
        } else {
            context.add_rc(self);
            let val: &T = &*self;
            size_of_val(val) + val.deep_size_of_children(context)
        }
    }
}

// References aren't owned; should they count?
impl<T> DeepSizeOf for &T
where
    T: DeepSizeOf + ?Sized,
{
    fn deep_size_of_children(&self, _context: &mut Context) -> usize {
        0
        // if context.contains_ref(&self) {
        //     0
        // } else {
        //     context.add_ref(&self);
        //     size_of_val(*self) + (*self).deep_size_of_children(context)
        // }
    }
}

impl<T> DeepSizeOf for &mut T
where
    T: DeepSizeOf + ?Sized,
{
    fn deep_size_of_children(&self, _context: &mut Context) -> usize {
        0
    }
}

impl<T> DeepSizeOf for [T]
where
    T: DeepSizeOf,
{
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        self.iter()
            .map(|child| child.deep_size_of_children(context))
            .sum()
    }
}
