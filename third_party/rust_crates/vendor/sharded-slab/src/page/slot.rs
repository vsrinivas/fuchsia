use super::FreeList;
use crate::sync::{
    atomic::{self, AtomicUsize, Ordering},
    UnsafeCell,
};
use crate::{cfg, clear::Clear, Pack, Tid};
use std::{fmt, marker::PhantomData};

pub(crate) struct Slot<T, C> {
    lifecycle: AtomicUsize,
    /// The offset of the next item on the free list.
    next: UnsafeCell<usize>,
    /// The data stored in the slot.
    item: UnsafeCell<T>,
    _cfg: PhantomData<fn(C)>,
}

#[derive(Debug)]
pub(crate) struct Guard<'a, T, C = cfg::DefaultConfig> {
    item: &'a T,
    lifecycle: &'a AtomicUsize,
    _cfg: PhantomData<fn(C)>,
}

#[repr(transparent)]
pub(crate) struct Generation<C = cfg::DefaultConfig> {
    value: usize,
    _cfg: PhantomData<fn(C)>,
}

#[repr(transparent)]
pub(crate) struct RefCount<C = cfg::DefaultConfig> {
    value: usize,
    _cfg: PhantomData<fn(C)>,
}

pub(crate) struct Lifecycle<C> {
    state: State,
    _cfg: PhantomData<fn(C)>,
}
struct LifecycleGen<C>(Generation<C>);

#[derive(Debug, Eq, PartialEq, Copy, Clone)]
#[repr(usize)]
enum State {
    NotRemoved = 0b00,
    Marked = 0b01,
    Removing = 0b11,
}

impl<C: cfg::Config> Pack<C> for Generation<C> {
    /// Use all the remaining bits in the word for the generation counter, minus
    /// any bits reserved by the user.
    const LEN: usize = (cfg::WIDTH - C::RESERVED_BITS) - Self::SHIFT;

    type Prev = Tid<C>;

    #[inline(always)]
    fn from_usize(u: usize) -> Self {
        debug_assert!(u <= Self::BITS);
        Self::new(u)
    }

    #[inline(always)]
    fn as_usize(&self) -> usize {
        self.value
    }
}

impl<C: cfg::Config> Generation<C> {
    fn new(value: usize) -> Self {
        Self {
            value,
            _cfg: PhantomData,
        }
    }
}

// Slot methods which should work across all trait bounds
impl<T, C> Slot<T, C>
where
    C: cfg::Config,
{
    #[inline(always)]
    pub(super) fn next(&self) -> usize {
        self.next.with(|next| unsafe { *next })
    }

    #[inline(always)]
    pub(super) fn value(&self) -> &T {
        self.item.with(|item| unsafe { &*item })
    }

    #[inline(always)]
    pub(super) fn set_next(&self, next: usize) {
        self.next.with_mut(|n| unsafe {
            (*n) = next;
        })
    }

    #[inline(always)]
    pub(in crate::page) fn get<U>(
        &self,
        gen: Generation<C>,
        f: impl FnOnce(&T) -> &U,
    ) -> Option<Guard<'_, U, C>> {
        let mut lifecycle = self.lifecycle.load(Ordering::Acquire);
        loop {
            // Unpack the current state.
            let state = Lifecycle::<C>::from_packed(lifecycle);
            let current_gen = LifecycleGen::<C>::from_packed(lifecycle).0;
            let refs = RefCount::<C>::from_packed(lifecycle);

            test_println!(
                "-> get {:?}; current_gen={:?}; lifecycle={:#x}; state={:?}; refs={:?};",
                gen,
                current_gen,
                lifecycle,
                state,
                refs,
            );

            // Is it okay to access this slot? The accessed generation must be
            // current, and the slot must not be in the process of being
            // removed. If we can no longer access the slot at the given
            // generation, return `None`.
            if gen != current_gen || state != Lifecycle::NOT_REMOVED {
                test_println!("-> get: no longer exists!");
                return None;
            }

            // Would incrementing the ref count cause an overflow?
            if refs.value >= RefCount::<C>::MAX {
                test_println!(
                    "-> get: max concurrent references ({}) reached!",
                    RefCount::<C>::MAX
                );
                return None;
            }

            // Try to increment the slot's ref count by one.
            let new_refs = refs.incr();
            match self.lifecycle.compare_exchange(
                lifecycle,
                new_refs.pack(current_gen.pack(state.pack(0))),
                Ordering::AcqRel,
                Ordering::Acquire,
            ) {
                Ok(_) => {
                    // Okay, the ref count was incremented successfully! We can
                    // now return a guard!
                    let item = f(self.value());

                    test_println!("-> {:?}", new_refs);

                    return Some(Guard {
                        item,
                        lifecycle: &self.lifecycle,
                        _cfg: PhantomData,
                    });
                }
                Err(actual) => {
                    // Another thread modified the slot's state before us! We
                    // need to retry with the new state.
                    //
                    // Since the new state may mean that the accessed generation
                    // is no longer valid, we'll check again on the next
                    // iteration of the loop.
                    test_println!("-> get: retrying; lifecycle={:#x};", actual);
                    lifecycle = actual;
                }
            };
        }
    }

    /// Marks this slot for mutation
    ///
    /// This method checks if there are any references to this slot. If there _are_ valid
    /// references, it just marks them for modification and returns and the next thread calling
    /// either `clear_storage` or `remove_value` will try and modify the storage
    fn mark_release(&self, gen: Generation<C>) -> bool {
        let mut lifecycle = self.lifecycle.load(Ordering::Acquire);
        let mut curr_gen;

        // Try to advance the slot's state to "MARKED", which indicates that it
        // should be removed when it is no longer concurrently accessed.
        loop {
            curr_gen = LifecycleGen::from_packed(lifecycle).0;
            test_println!(
                "-> mark_release; gen={:?}; current_gen={:?};",
                gen,
                curr_gen
            );

            // Is the slot still at the generation we are trying to remove?
            if gen != curr_gen {
                return false;
            }

            // Set the new state to `MARKED`.
            let new_lifecycle = Lifecycle::<C>::MARKED.pack(lifecycle);
            test_println!(
                "-> mark_release; old_lifecycle={:#x}; new_lifecycle={:#x};",
                lifecycle,
                new_lifecycle
            );

            match self.lifecycle.compare_exchange(
                lifecycle,
                new_lifecycle,
                Ordering::AcqRel,
                Ordering::Acquire,
            ) {
                Ok(_) => break,
                Err(actual) => {
                    test_println!("-> mark_release; retrying");
                    lifecycle = actual;
                }
            }
        }

        // Unpack the current reference count to see if we can remove the slot now.
        let refs = RefCount::<C>::from_packed(lifecycle);
        test_println!("-> mark_release: marked; refs={:?};", refs);

        // Are there currently outstanding references to the slot? If so, it
        // will have to be removed when those references are dropped.
        refs.value > 0
    }

    /// Mutates this slot.
    ///
    /// This method spins until no references to this slot are left, and calls the mutator
    fn release_with<F, M, R>(&self, gen: Generation<C>, offset: usize, free: &F, mutator: M) -> R
    where
        F: FreeList<C>,
        M: FnOnce(Option<&mut T>) -> R,
    {
        let mut lifecycle = self.lifecycle.load(Ordering::Acquire);
        let mut advanced = false;
        // Exponential spin backoff while waiting for the slot to be released.
        let mut spin_exp = 0;
        let next_gen = gen.advance();
        loop {
            let current_gen = Generation::from_packed(lifecycle);
            test_println!("-> release_with; lifecycle={:#x}; expected_gen={:?}; current_gen={:?}; next_gen={:?};",
                lifecycle,
                gen,
                current_gen,
                next_gen
            );

            // First, make sure we are actually able to remove the value.
            // If we're going to remove the value, the generation has to match
            // the value that `remove_value` was called with...unless we've
            // already stored the new generation.
            if (!advanced) && gen != current_gen {
                test_println!("-> already removed!");
                return mutator(None);
            }

            match self.lifecycle.compare_exchange(
                lifecycle,
                next_gen.pack(lifecycle),
                Ordering::AcqRel,
                Ordering::Acquire,
            ) {
                Ok(actual) => {
                    // If we're in this state, we have successfully advanced to
                    // the next generation.
                    advanced = true;

                    // Make sure that there are no outstanding references.
                    let refs = RefCount::<C>::from_packed(actual);
                    test_println!("-> advanced gen; lifecycle={:#x}; refs={:?};", actual, refs);
                    if refs.value == 0 {
                        test_println!("-> ok to remove!");
                        // safety: we've modified the generation of this slot and any other thread
                        // calling this method will exit out at the generation check above in the
                        // next iteraton of the loop.
                        let value = self
                            .item
                            .with_mut(|item| mutator(Some(unsafe { &mut *item })));
                        free.push(offset, self);
                        return value;
                    }

                    // Otherwise, a reference must be dropped before we can
                    // remove the value. Spin here until there are no refs remaining...
                    test_println!("-> refs={:?}; spin...", refs);

                    // Back off, spinning and possibly yielding.
                    exponential_backoff(&mut spin_exp);
                }
                Err(actual) => {
                    test_println!("-> retrying; lifecycle={:#x};", actual);
                    lifecycle = actual;
                    // The state changed; reset the spin backoff.
                    spin_exp = 0;
                }
            }
        }
    }

    /// Initialize a slot
    ///
    /// This method initializes and sets up the state for a slot. When being used in `Pool`, we
    /// only need to ensure that the `Slot` is in the right state, while when being used in a
    /// `Slab` we want to insert a value into it, as the memory is not initialized
    pub(crate) fn initialize_state(&self, f: impl FnOnce(&mut T)) -> Option<Generation<C>> {
        // Load the current lifecycle state.
        let lifecycle = self.lifecycle.load(Ordering::Acquire);
        let gen = LifecycleGen::from_packed(lifecycle).0;
        let refs = RefCount::<C>::from_packed(lifecycle);

        test_println!(
            "-> initialize_state; state={:?}; gen={:?}; refs={:?}",
            Lifecycle::<C>::from_packed(lifecycle),
            gen,
            refs
        );

        if refs.value != 0 {
            test_println!("-> initialize while referenced! cancelling");
            return None;
        }

        // Set the slot's state to NOT_REMOVED.
        let new_lifecycle = gen.pack(Lifecycle::<C>::NOT_REMOVED.pack(0));
        let actual = self
            .lifecycle
            .compare_and_swap(lifecycle, new_lifecycle, Ordering::AcqRel);
        if actual != lifecycle {
            // The slot was modified while we were inserting to it! It's no
            // longer safe to insert a new value.
            test_println!(
                "-> modified during insert, cancelling! new={:#x}; expected={:#x}; actual={:#x};",
                new_lifecycle,
                lifecycle,
                actual
            );
            return None;
        }

        // call provided function to update this slot
        self.item.with_mut(|item| unsafe {
            f(&mut *item);
        });

        Some(gen)
    }
}

// Slot impl which _needs_ an `Option` for self.item, this is for `Slab` to use.
impl<T, C> Slot<Option<T>, C>
where
    C: cfg::Config,
{
    fn is_empty(&self) -> bool {
        self.item.with(|item| unsafe { (*item).is_none() })
    }

    /// Insert a value into a slot
    ///
    /// We first initialize the state and then insert the pased in value into the slot.
    #[inline]
    pub(crate) fn insert(&self, value: &mut Option<T>) -> Option<Generation<C>> {
        debug_assert!(self.is_empty(), "inserted into full slot");
        debug_assert!(value.is_some(), "inserted twice");

        let gen = self.initialize_state(|item| {
            *item = value.take();
        })?;
        test_println!("-> inserted at {:?}", gen);

        Some(gen)
    }

    /// Tries to remove the value in the slot
    ///
    /// This method tries to remove the value in the slot. If there are existing references, then
    /// the slot is marked for removal and the next thread calling either this method or
    /// `remove_value` will do the work instead.
    #[inline]
    pub(super) fn try_remove_value<F: FreeList<C>>(
        &self,
        gen: Generation<C>,
        offset: usize,
        free: &F,
    ) -> Option<T> {
        if self.mark_release(gen) {
            None
        } else {
            // Otherwise, we can remove the slot now!
            test_println!("-> try_remove_value; can remove now");
            self.remove_value(gen, offset, free)
        }
    }

    #[inline]
    pub(super) fn remove_value<F: FreeList<C>>(
        &self,
        gen: Generation<C>,
        offset: usize,
        free: &F,
    ) -> Option<T> {
        self.release_with(gen, offset, free, |item| item.and_then(Option::take))
    }
}

// These impls are specific to `Pool`
impl<T, C> Slot<T, C>
where
    T: Default + Clear,
    C: cfg::Config,
{
    pub(in crate::page) fn new(next: usize) -> Self {
        Self {
            lifecycle: AtomicUsize::new(0),
            item: UnsafeCell::new(T::default()),
            next: UnsafeCell::new(next),
            _cfg: PhantomData,
        }
    }

    /// Try to clear this slot's storage
    ///
    /// If there are references to this slot, then we mark this slot for clearing and let the last
    /// thread do the work for us.
    #[inline]
    pub(super) fn try_clear_storage<F: FreeList<C>>(
        &self,
        gen: Generation<C>,
        offset: usize,
        free: &F,
    ) -> bool {
        if self.mark_release(gen) {
            return false;
        }

        // Otherwise, we can remove the slot now!
        test_println!("-> try_clear_storage; can remove now");
        self.clear_storage(gen, offset, free)
    }

    /// Clear this slot's storage
    ///
    /// This method blocks until all references have been dropped and clears the storage.
    pub(super) fn clear_storage<F: FreeList<C>>(
        &self,
        gen: Generation<C>,
        offset: usize,
        free: &F,
    ) -> bool {
        // release_with will _always_ wait unitl it can release the slot or just return if the slot
        // has already been released.
        self.release_with(gen, offset, free, |item| {
            let cleared = item.map(|inner| Clear::clear(inner)).is_some();
            test_println!("-> cleared: {}", cleared);
            cleared
        })
    }
}

impl<T, C: cfg::Config> fmt::Debug for Slot<T, C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let lifecycle = self.lifecycle.load(Ordering::Relaxed);
        f.debug_struct("Slot")
            .field("lifecycle", &format_args!("{:#x}", lifecycle))
            .field("state", &Lifecycle::<C>::from_packed(lifecycle).state)
            .field("gen", &LifecycleGen::<C>::from_packed(lifecycle).0)
            .field("refs", &RefCount::<C>::from_packed(lifecycle))
            .field("next", &self.next())
            .finish()
    }
}

// === impl Generation ===

impl<C> fmt::Debug for Generation<C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_tuple("Generation").field(&self.value).finish()
    }
}

impl<C: cfg::Config> Generation<C> {
    fn advance(self) -> Self {
        Self::from_usize((self.value + 1) % Self::BITS)
    }
}

impl<C: cfg::Config> PartialEq for Generation<C> {
    fn eq(&self, other: &Self) -> bool {
        self.value == other.value
    }
}

impl<C: cfg::Config> Eq for Generation<C> {}

impl<C: cfg::Config> PartialOrd for Generation<C> {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        self.value.partial_cmp(&other.value)
    }
}

impl<C: cfg::Config> Ord for Generation<C> {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.value.cmp(&other.value)
    }
}

impl<C: cfg::Config> Clone for Generation<C> {
    fn clone(&self) -> Self {
        Self::new(self.value)
    }
}

impl<C: cfg::Config> Copy for Generation<C> {}

// === impl Guard ===

impl<'a, T, C: cfg::Config> Guard<'a, T, C> {
    pub(crate) fn release(&self) -> bool {
        let mut lifecycle = self.lifecycle.load(Ordering::Acquire);
        loop {
            let refs = RefCount::<C>::from_packed(lifecycle);
            let state = Lifecycle::<C>::from_packed(lifecycle).state;
            let gen = LifecycleGen::<C>::from_packed(lifecycle).0;

            // Are we the last guard, and is the slot marked for removal?
            let dropping = refs.value == 1 && state == State::Marked;
            let new_lifecycle = if dropping {
                // If so, we want to advance the state to "removing"
                gen.pack(State::Removing as usize)
            } else {
                // Otherwise, just subtract 1 from the ref count.
                refs.decr().pack(lifecycle)
            };

            test_println!(
                "-> drop guard: state={:?}; gen={:?}; refs={:?}; lifecycle={:#x}; new_lifecycle={:#x}; dropping={:?}",
                state,
                gen,
                refs,
                lifecycle,
                new_lifecycle,
                dropping
            );
            match self.lifecycle.compare_exchange(
                lifecycle,
                new_lifecycle,
                Ordering::AcqRel,
                Ordering::Acquire,
            ) {
                Ok(_) => {
                    test_println!("-> drop guard: done;  dropping={:?}", dropping);
                    return dropping;
                }
                Err(actual) => {
                    test_println!("-> drop guard; retry, actual={:#x}", actual);
                    lifecycle = actual;
                }
            }
        }
    }

    pub(crate) fn item(&self) -> &T {
        self.item
    }
}

// === impl Lifecycle ===

impl<C: cfg::Config> Lifecycle<C> {
    const MARKED: Self = Self {
        state: State::Marked,
        _cfg: PhantomData,
    };

    const NOT_REMOVED: Self = Self {
        state: State::NotRemoved,
        _cfg: PhantomData,
    };
}

impl<C: cfg::Config> Pack<C> for Lifecycle<C> {
    const LEN: usize = 2;
    type Prev = ();

    fn from_usize(u: usize) -> Self {
        Self {
            state: match u & Self::MASK {
                0b00 => State::NotRemoved,
                0b01 => State::Marked,
                0b11 => State::Removing,
                bad => unreachable!("weird lifecycle {:#b}", bad),
            },
            _cfg: PhantomData,
        }
    }

    fn as_usize(&self) -> usize {
        self.state as usize
    }
}

impl<C> PartialEq for Lifecycle<C> {
    fn eq(&self, other: &Self) -> bool {
        self.state == other.state
    }
}

impl<C> Eq for Lifecycle<C> {}

impl<C> fmt::Debug for Lifecycle<C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_tuple("Lifecycle").field(&self.state).finish()
    }
}

// === impl RefCount ===

impl<C: cfg::Config> Pack<C> for RefCount<C> {
    const LEN: usize = cfg::WIDTH - (Lifecycle::<C>::LEN + Generation::<C>::LEN);
    type Prev = Lifecycle<C>;

    fn from_usize(value: usize) -> Self {
        debug_assert!(value <= Self::MAX);
        Self {
            value,
            _cfg: PhantomData,
        }
    }

    fn as_usize(&self) -> usize {
        self.value
    }
}

impl<C: cfg::Config> RefCount<C> {
    pub(crate) const MAX: usize = Self::BITS;

    #[inline]
    fn incr(self) -> Self {
        // It's okay for this to be a debug assertion, because the check in
        // `Slot::get` should protect against incrementing the reference count
        // if it would overflow. This is intended to test that the check is in
        // place.
        debug_assert!(
            self.value < Self::MAX,
            "incrementing ref count would overflow max value ({})",
            Self::MAX
        );
        Self::from_usize(self.value + 1)
    }

    #[inline]
    fn decr(self) -> Self {
        Self::from_usize(self.value - 1)
    }
}

impl<C> fmt::Debug for RefCount<C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_tuple("RefCount").field(&self.value).finish()
    }
}

impl<C: cfg::Config> PartialEq for RefCount<C> {
    fn eq(&self, other: &Self) -> bool {
        self.value == other.value
    }
}

impl<C: cfg::Config> Eq for RefCount<C> {}

impl<C: cfg::Config> PartialOrd for RefCount<C> {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        self.value.partial_cmp(&other.value)
    }
}

impl<C: cfg::Config> Ord for RefCount<C> {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.value.cmp(&other.value)
    }
}

impl<C: cfg::Config> Clone for RefCount<C> {
    fn clone(&self) -> Self {
        Self::from_usize(self.value)
    }
}

impl<C: cfg::Config> Copy for RefCount<C> {}

// === impl LifecycleGen ===

impl<C: cfg::Config> Pack<C> for LifecycleGen<C> {
    const LEN: usize = Generation::<C>::LEN;
    type Prev = RefCount<C>;

    fn from_usize(value: usize) -> Self {
        Self(Generation::from_usize(value))
    }

    fn as_usize(&self) -> usize {
        self.0.as_usize()
    }
}

// === helpers ===

#[inline(always)]
fn exponential_backoff(exp: &mut usize) {
    /// Maximum exponent we can back off to.
    const MAX_EXPONENT: usize = 8;

    // Issue 2^exp pause instructions.
    for _ in 0..(1 << *exp) {
        atomic::spin_loop_hint();
    }

    if *exp >= MAX_EXPONENT {
        // If we have reached the max backoff, also yield to the scheduler
        // explicitly.
        crate::sync::yield_now();
    } else {
        // Otherwise, increment the exponent.
        *exp += 1;
    }
}
