//! Mutexes can deadlock each other, but you can avoid this by always acquiring your locks in a
//! consistent order. This crate provides tracing to ensure that you do.
//!
//! This crate tracks a virtual "stack" of locks that the current thread holds, and whenever a new
//! lock is acquired, a dependency is created from the last lock to the new one. These dependencies
//! together form a graph. As long as that graph does not contain any cycles, your program is
//! guaranteed to never deadlock.
//!
//! # Panics
//!
//! The primary method by which this crate signals an invalid lock acquisition order is by
//! panicking. When a cycle is created in the dependency graph when acquiring a lock, the thread
//! will instead panic. This panic will not poison the underlying mutex.
//!
//! This conflicting dependency is not added to the graph, so future attempts at locking should
//! succeed as normal.
//!
//! # Structure
//!
//! Each module in this crate exposes wrappers for a specific base-mutex with dependency trakcing
//! added. For now, that is limited to [`stdsync`] which provides wrappers for the base locks in the
//! standard library. More back-ends may be added as features in the future.
//!
//! # Performance considerations
//!
//! Tracing a mutex adds overhead to certain mutex operations in order to do the required
//! bookkeeping. The following actions have the following overhead.
//!
//! - **Acquiring a lock** locks the global dependency graph temporarily to check if the new lock
//!   would introduce a cyclic dependency. This crate uses the algorithm proposed in ["A Dynamic
//!   Topological Sort Algorithm for Directed Acyclic Graphs" by David J. Pearce and Paul H.J.
//!   Kelly][paper] to detect cycles as efficently as possible. In addition, a thread local lock set
//!   is updated with the new lock.
//!
//! - **Releasing a lock** updates a thread local lock set to remove the released lock.
//!
//! - **Allocating a lock** performs an atomic update to a shared counter.
//!
//! - **Deallocating a mutex** temporarily locks the global dependency graph to remove the lock
//!   entry in the dependency graph.
//!
//! These operations have been reasonably optimized, but the performance penalty may yet be too much
//! for production use. In those cases, it may be beneficial to instead use debug-only versions
//! (such as [`stdsync::DebugMutex`]) which evaluate to a tracing mutex when debug assertions are
//! enabled, and to the underlying mutex when they're not.
//!
//! [paper]: https://whileydave.com/publications/pk07_jea/
#![cfg_attr(docsrs, feature(doc_cfg))]
use std::cell::RefCell;
use std::cell::UnsafeCell;
use std::fmt;
use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::ops::Deref;
use std::ops::DerefMut;
use std::ptr;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;
use std::sync::Mutex;
use std::sync::Once;
use std::sync::PoisonError;

use lazy_static::lazy_static;
#[cfg(feature = "lockapi")]
#[cfg_attr(docsrs, doc(cfg(feature = "lockapi")))]
pub use lock_api;
#[cfg(feature = "parkinglot")]
#[cfg_attr(docsrs, doc(cfg(feature = "parkinglot")))]
pub use parking_lot;

use crate::graph::DiGraph;

mod graph;
#[cfg(feature = "lockapi")]
#[cfg_attr(docsrs, doc(cfg(feature = "lockapi")))]
pub mod lockapi;
#[cfg(feature = "parkinglot")]
#[cfg_attr(docsrs, doc(cfg(feature = "parkinglot")))]
pub mod parkinglot;
pub mod stdsync;

/// Counter for Mutex IDs. Atomic avoids the need for locking.
///
/// Should be part of the `MutexID` impl but static items are not yet a thing.
static ID_SEQUENCE: AtomicUsize = AtomicUsize::new(0);

thread_local! {
    /// Stack to track which locks are held
    ///
    /// Assuming that locks are roughly released in the reverse order in which they were acquired,
    /// a stack should be more efficient to keep track of the current state than a set would be.
    static HELD_LOCKS: RefCell<Vec<usize>> = RefCell::new(Vec::new());
}

lazy_static! {
    static ref DEPENDENCY_GRAPH: Mutex<DiGraph<usize>> = Default::default();
}

/// Dedicated ID type for Mutexes
///
/// # Unstable
///
/// This type is currently private to prevent usage while the exact implementation is figured out,
/// but it will likely be public in the future.
struct MutexId(usize);

impl MutexId {
    /// Get a new, unique, mutex ID.
    ///
    /// This ID is guaranteed to be unique within the runtime of the program.
    ///
    /// # Panics
    ///
    /// This function may panic when there are no more mutex IDs available. The number of mutex ids
    /// is `usize::MAX - 1` which should be plenty for most practical applications.
    pub fn new() -> Self {
        ID_SEQUENCE
            .fetch_update(Ordering::SeqCst, Ordering::SeqCst, |id| id.checked_add(1))
            .map(Self)
            .expect("Mutex ID wraparound happened, results unreliable")
    }

    pub fn value(&self) -> usize {
        self.0
    }

    /// Get a borrowed guard for this lock.
    ///
    /// This method adds checks adds this Mutex ID to the dependency graph as needed, and adds the
    /// lock to the list of
    ///
    /// # Panics
    ///
    /// This method panics if the new dependency would introduce a cycle.
    pub fn get_borrowed(&self) -> BorrowedMutex {
        self.mark_held();
        BorrowedMutex(self)
    }

    /// Mark this lock as held for the purposes of dependency tracking.
    ///
    /// # Panics
    ///
    /// This method panics if the new dependency would introduce a cycle.
    pub fn mark_held(&self) {
        let creates_cycle = HELD_LOCKS.with(|locks| {
            if let Some(&previous) = locks.borrow().last() {
                let mut graph = get_dependency_graph();

                !graph.add_edge(previous, self.value())
            } else {
                false
            }
        });

        if creates_cycle {
            // Panic without holding the lock to avoid needlessly poisoning it
            panic!("Mutex order graph should not have cycles");
        }

        HELD_LOCKS.with(|locks| locks.borrow_mut().push(self.value()));
    }

    pub unsafe fn mark_released(&self) {
        HELD_LOCKS.with(|locks| {
            let mut locks = locks.borrow_mut();

            for (i, &lock) in locks.iter().enumerate().rev() {
                if lock == self.value() {
                    locks.remove(i);
                    return;
                }
            }

            // Drop impls shouldn't panic but if this happens something is seriously broken.
            unreachable!("Tried to drop lock for mutex {:?} but it wasn't held", self)
        });
    }
}

impl Default for MutexId {
    fn default() -> Self {
        Self::new()
    }
}

impl fmt::Debug for MutexId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "MutexID({:?})", self.0)
    }
}

impl Drop for MutexId {
    fn drop(&mut self) {
        get_dependency_graph().remove_node(self.value());
    }
}

/// `const`-compatible version of [`crate::MutexId`].
///
/// This struct can be used similarly to the normal mutex ID, but to be const-compatible its ID is
/// generated on first use. This allows it to be used as the mutex ID for mutexes with a `const`
/// constructor.
///
/// This type can be largely replaced once std::lazy gets stabilized.
struct LazyMutexId {
    inner: UnsafeCell<MaybeUninit<MutexId>>,
    setter: Once,
    _marker: PhantomData<MutexId>,
}

impl LazyMutexId {
    pub const fn new() -> Self {
        Self {
            inner: UnsafeCell::new(MaybeUninit::uninit()),
            setter: Once::new(),
            _marker: PhantomData,
        }
    }
}

impl fmt::Debug for LazyMutexId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self.deref())
    }
}

impl Default for LazyMutexId {
    fn default() -> Self {
        Self::new()
    }
}

/// Safety: the UnsafeCell is guaranteed to only be accessed mutably from a `Once`.
unsafe impl Sync for LazyMutexId {}

impl Deref for LazyMutexId {
    type Target = MutexId;

    fn deref(&self) -> &Self::Target {
        self.setter.call_once(|| {
            // Safety: this function is only called once, so only one mutable reference should exist
            // at a time.
            unsafe {
                *self.inner.get() = MaybeUninit::new(MutexId::new());
            }
        });

        // Safety: after the above Once runs, there are no longer any mutable references, so we can
        // hand this out safely.
        //
        // Explanation of this monstrosity:
        //
        // - Get a pointer to the data from the UnsafeCell
        // - Dereference that to get a reference to the underlying MaybeUninit
        // - Use as_ptr on MaybeUninit to get a pointer to the initialized MutexID
        // - Dereference the pointer to turn in into a reference as intended.
        //
        // This should get slightly nicer once `maybe_uninit_extra` is stabilized.
        unsafe { &*((*self.inner.get()).as_ptr()) }
    }
}

impl Drop for LazyMutexId {
    fn drop(&mut self) {
        if self.setter.is_completed() {
            // We have a valid mutex ID and need to drop it

            // Safety: we know that this pointer is valid because the initializer has successfully run.
            let mutex_id = unsafe { ptr::read((*self.inner.get()).as_ptr()) };

            drop(mutex_id);
        }
    }
}

#[derive(Debug)]
struct BorrowedMutex<'a>(&'a MutexId);

/// Drop a lock held by the current thread.
///
/// # Panics
///
/// This function panics if the lock did not appear to be handled by this thread. If that happens,
/// that is an indication of a serious design flaw in this library.
impl<'a> Drop for BorrowedMutex<'a> {
    fn drop(&mut self) {
        // Safety: the only way to get a BorrowedMutex is by locking the mutex.
        unsafe { self.0.mark_released() };
    }
}

/// Get a reference to the current dependency graph
fn get_dependency_graph() -> impl DerefMut<Target = DiGraph<usize>> {
    DEPENDENCY_GRAPH
        .lock()
        .unwrap_or_else(PoisonError::into_inner)
}

#[cfg(test)]
mod tests {
    use rand::seq::SliceRandom;
    use rand::thread_rng;

    use super::*;

    #[test]
    fn test_next_mutex_id() {
        let initial = MutexId::new();
        let next = MutexId::new();

        // Can't assert N + 1 because multiple threads running tests
        assert!(initial.0 < next.0);
    }

    #[test]
    fn test_lazy_mutex_id() {
        let a = LazyMutexId::new();
        let b = LazyMutexId::new();
        let c = LazyMutexId::new();

        let mut graph = get_dependency_graph();
        assert!(graph.add_edge(a.value(), b.value()));
        assert!(graph.add_edge(b.value(), c.value()));

        // Creating an edge c → a should fail as it introduces a cycle.
        assert!(!graph.add_edge(c.value(), a.value()));

        // Drop graph handle so we can drop vertices without deadlocking
        drop(graph);

        drop(b);

        // If b's destructor correctly ran correctly we can now add an edge from c to a.
        assert!(get_dependency_graph().add_edge(c.value(), a.value()));
    }

    /// Test creating a cycle, then panicking.
    #[test]
    #[should_panic]
    fn test_mutex_id_conflict() {
        let ids = [MutexId::new(), MutexId::new(), MutexId::new()];

        for i in 0..3 {
            let _first_lock = ids[i].get_borrowed();
            let _second_lock = ids[(i + 1) % 3].get_borrowed();
        }
    }

    /// Fuzz the global dependency graph by fake-acquiring lots of mutexes in a valid order.
    ///
    /// This test generates all possible forward edges in a 100-node graph consisting of natural
    /// numbers, shuffles them, then adds them to the graph. This will always be a valid directed,
    /// acyclic graph because there is a trivial order (the natural numbers) but because the edges
    /// are added in a random order the DiGraph will still occassionally need to reorder nodes.
    #[test]
    fn fuzz_mutex_id() {
        const NUM_NODES: usize = 100;

        let ids: Vec<MutexId> = (0..NUM_NODES).map(|_| Default::default()).collect();

        let mut edges = Vec::with_capacity(NUM_NODES * NUM_NODES);
        for i in 0..NUM_NODES {
            for j in i..NUM_NODES {
                if i != j {
                    edges.push((i, j));
                }
            }
        }

        edges.shuffle(&mut thread_rng());

        for (x, y) in edges {
            // Acquire the mutexes, smallest first to ensure a cycle-free graph
            let _ignored = ids[x].get_borrowed();
            let _ = ids[y].get_borrowed();
        }
    }
}
