use crate::{
    cfg::{self, CfgPrivate, DefaultConfig},
    clear::Clear,
    page,
    tid::Tid,
    Pack, Shard,
};

use std::{fmt, marker::PhantomData};

/// A lock-free concurrent object pool.
///
/// Slabs provide pre-allocated storage for many instances of a single type. But, when working with
/// heap allocated objects, the advantages of a slab are lost, as the memory allocated for the
/// object is freed when the object is removed from the slab. With a pool, we can instead reuse
/// this memory for objects being added to the pool in the future, therefore reducing memory
/// fragmentation and avoiding additional allocations.
///
/// This type implements a lock-free concurrent pool, indexed by `usize`s. The items stored in this
/// type need to implement [`Clear`] and `Default`.
///
/// The `Pool` type shares similar semantics to [`Slab`] when it comes to sharing across threads
/// and storing mutable shared data. The biggest difference is there are no [`Slab::insert`] and
/// [`Slab::take`] analouges for the `Pool` type. Instead new items are added to the pool by using
/// the [`Pool::create`] method, and marked for clearing by the [`Pool::clear`] method.
///
/// # Examples
///
/// Add an entry to the pool, returning an index:
/// ```
/// # use sharded_slab::Pool;
/// let pool: Pool<String> = Pool::new();
///
/// let key = pool.create(|item| item.push_str("hello world")).unwrap();
/// assert_eq!(pool.get(key).unwrap(), String::from("hello world"));
/// ```
///
/// Pool entries can be cleared either by manually calling [`Pool::clear`]. This marks the entry to
/// be cleared when the guards referencing to it are dropped.
/// ```
/// # use sharded_slab::Pool;
/// let pool: Pool<String> = Pool::new();
///
/// let key = pool.create(|item| item.push_str("hello world")).unwrap();
///
/// // Mark this entry to be cleared.
/// pool.clear(key);
///
/// // The cleared entry is no longer available in the pool
/// assert!(pool.get(key).is_none());
/// ```
/// # Configuration
///
/// Both `Pool` and [`Slab`] share the same configuration mechanism. See [crate level documentation][config-doc]
/// for more details.
///
/// [`Slab::take`]: ../struct.Slab.html#method.take
/// [`Slab::insert`]: ../struct.Slab.html#method.insert
/// [`Pool::create`]: struct.Pool.html#method.create
/// [`Pool::clear`]: struct.Pool.html#method.clear
/// [config-doc]: ../index.html#configuration
/// [`Clear`]: trait.Clear.html
/// [`Slab`]: struct.Slab.html
pub struct Pool<T, C = DefaultConfig>
where
    T: Clear + Default,
    C: cfg::Config,
{
    shards: Box<[Shard<T, C>]>,
    _cfg: PhantomData<C>,
}

impl<T> Pool<T>
where
    T: Clear + Default,
{
    pub fn new() -> Self {
        Self::new_with_config()
    }

    /// Returns a new `Pool` with the provided configuration parameters.
    pub fn new_with_config<C: cfg::Config>() -> Pool<T, C> {
        C::validate();
        let shards = (0..C::MAX_SHARDS).map(Shard::new).collect();
        Pool {
            shards,
            _cfg: PhantomData,
        }
    }
}

/// A guard that allows access to an object in a pool.
///
/// While the guard exists, it indicates to the pool that the item the guard references is
/// currently being accessed. If the item is removed from the pool while the guard exists, the
/// removal will be deferred until all guards are dropped.
pub struct PoolGuard<'a, T, C>
where
    T: Clear + Default,
    C: cfg::Config,
{
    inner: page::slot::Guard<'a, T, C>,
    shard: &'a Shard<T, C>,
    key: usize,
}

impl<T, C> Pool<T, C>
where
    T: Clear + Default,
    C: cfg::Config,
{
    /// The number of bits in each index which are used by the pool.
    ///
    /// If other data is packed into the `usize` indices returned by
    /// [`Pool::create`], user code is free to use any bits higher than the
    /// `USED_BITS`-th bit freely.
    ///
    /// This is determined by the [`Config`] type that configures the pool's
    /// parameters. By default, all bits are used; this can be changed by
    /// overriding the [`Config::RESERVED_BITS`][res] constant.
    ///
    /// [`Config`]: trait.Config.html
    /// [res]: trait.Config.html#associatedconstant.RESERVED_BITS
    /// [`Slab::insert`]: struct.Slab.html#method.insert
    pub const USED_BITS: usize = C::USED_BITS;

    /// Creates a new object in the pool, returning a key that can be used to access it.
    ///
    /// If this function returns `None`, then the shard for the current thread is full and no items
    /// can be added until some are removed, or the maximum number of shards has been reached.
    ///
    /// # Examples
    /// ```rust
    /// # use sharded_slab::Pool;
    /// let pool: Pool<String> = Pool::new();
    /// let key = pool.create(|item| item.push_str("Hello")).unwrap();
    /// assert_eq!(pool.get(key).unwrap(), String::from("Hello"));
    /// ```
    pub fn create(&self, initializer: impl FnOnce(&mut T)) -> Option<usize> {
        let tid = Tid::<C>::current();
        let mut init = Some(initializer);
        test_println!("pool: create {:?}", tid);
        self.shards[tid.as_usize()]
            .init_with(|slot| {
                let init = init.take().expect("initializer will only be called once");
                slot.initialize_state(init)
            })
            .map(|idx| tid.pack(idx))
    }

    /// Return a reference to the value associated with the given key.
    ///
    /// If the pool does not contain a value for the given key, `None` is returned instead.
    ///
    /// # Examples
    ///
    /// ```rust
    /// # use sharded_slab::Pool;
    /// let pool: Pool<String> = sharded_slab::Pool::new();
    /// let key = pool.create(|item| item.push_str("hello world")).unwrap();
    ///
    /// assert_eq!(pool.get(key).unwrap(), String::from("hello world"));
    /// assert!(pool.get(12345).is_none());
    /// ```
    pub fn get(&self, key: usize) -> Option<PoolGuard<'_, T, C>> {
        let tid = C::unpack_tid(key);

        test_println!("pool: get{:?}; current={:?}", tid, Tid::<C>::current());
        let shard = self.shards.get(tid.as_usize())?;
        let inner = shard.get(key, |x| x)?;

        Some(PoolGuard { inner, shard, key })
    }

    /// Remove the value using the storage associated with the given key from the pool, returning
    /// `true` if the value was removed.
    ///
    /// This method does _not_ block the current thread until the value can be
    /// cleared. Instead, if another thread is currently accessing that value, this marks it to be
    /// cleared by that thread when it is done accessing that value.
    ///
    /// # Examples
    ///
    /// ```rust
    /// # use sharded_slab::Pool;
    /// let pool: Pool<String> = Pool::new();
    /// let key = pool.create(|item| item.push_str("hello world")).unwrap();
    ///
    /// assert_eq!(pool.get(key).unwrap(), String::from("hello world"));
    ///
    /// pool.clear(key);
    /// assert!(pool.get(key).is_none());
    /// ```
    ///
    /// ```
    /// # use sharded_slab::Pool;
    /// let pool: Pool<String> = Pool::new();
    ///
    /// let key = pool.create(|item| item.push_str("Hello world!")).unwrap();
    ///
    /// // Clearing a key that doesn't exist in the `Pool` will return `false`
    /// assert_eq!(pool.clear(key + 69420), false);
    ///
    /// // Clearing a key that does exist returns `true`
    /// assert!(pool.clear(key));
    ///
    /// // Clearing a key that has previously been cleared will return `false`
    /// assert_eq!(pool.clear(key), false);
    /// ```
    /// [`clear`]: #method.clear
    pub fn clear(&self, key: usize) -> bool {
        let tid = C::unpack_tid(key);

        let shard = self.shards.get(tid.as_usize());
        if tid.is_current() {
            shard
                .map(|shard| shard.mark_clear_local(key))
                .unwrap_or(false)
        } else {
            shard
                .map(|shard| shard.mark_clear_remote(key))
                .unwrap_or(false)
        }
    }
}

unsafe impl<T, C> Send for Pool<T, C>
where
    T: Send + Clear + Default,
    C: cfg::Config,
{
}
unsafe impl<T, C> Sync for Pool<T, C>
where
    T: Sync + Clear + Default,
    C: cfg::Config,
{
}

impl<T> Default for Pool<T>
where
    T: Clear + Default,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, C> fmt::Debug for Pool<T, C>
where
    T: fmt::Debug + Clear + Default,
    C: cfg::Config,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Pool")
            .field("shards", &self.shards)
            .field("config", &C::debug())
            .finish()
    }
}

impl<'a, T, C> PoolGuard<'a, T, C>
where
    T: Clear + Default,
    C: cfg::Config,
{
    /// Returns the key used to access this guard
    pub fn key(&self) -> usize {
        self.key
    }
}

impl<'a, T, C> std::ops::Deref for PoolGuard<'a, T, C>
where
    T: Clear + Default,
    C: cfg::Config,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self.inner.item()
    }
}

impl<'a, T, C> Drop for PoolGuard<'a, T, C>
where
    T: Clear + Default,
    C: cfg::Config,
{
    fn drop(&mut self) {
        use crate::sync::atomic;
        test_println!(" -> drop PoolGuard: clearing data");
        if self.inner.release() {
            atomic::fence(atomic::Ordering::Acquire);
            if Tid::<C>::current().as_usize() == self.shard.tid {
                self.shard.mark_clear_local(self.key);
            } else {
                self.shard.mark_clear_remote(self.key);
            }
        }
    }
}

impl<'a, T, C> fmt::Debug for PoolGuard<'a, T, C>
where
    T: fmt::Debug + Clear + Default,
    C: cfg::Config,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(self.inner.item(), f)
    }
}

impl<'a, T, C> PartialEq<T> for PoolGuard<'a, T, C>
where
    T: PartialEq<T> + Clear + Default,
    C: cfg::Config,
{
    fn eq(&self, other: &T) -> bool {
        *self.inner.item() == *other
    }
}

#[cfg(test)]
mod tests;
