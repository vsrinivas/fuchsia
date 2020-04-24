use crate::{
    cfg::{self, CfgPrivate},
    clear::Clear,
    page,
    tid::Tid,
    Pack,
};

use std::fmt;

// ┌─────────────┐      ┌────────┐
// │ page 1      │      │        │
// ├─────────────┤ ┌───▶│  next──┼─┐
// │ page 2      │ │    ├────────┤ │
// │             │ │    │XXXXXXXX│ │
// │ local_free──┼─┘    ├────────┤ │
// │ global_free─┼─┐    │        │◀┘
// ├─────────────┤ └───▶│  next──┼─┐
// │   page 3    │      ├────────┤ │
// └─────────────┘      │XXXXXXXX│ │
//       ...            ├────────┤ │
// ┌─────────────┐      │XXXXXXXX│ │
// │ page n      │      ├────────┤ │
// └─────────────┘      │        │◀┘
//                      │  next──┼───▶
//                      ├────────┤
//                      │XXXXXXXX│
//                      └────────┘
//                         ...
pub(crate) struct Shard<T, C: cfg::Config> {
    /// The shard's parent thread ID.
    pub(crate) tid: usize,
    /// The local free list for each page.
    ///
    /// These are only ever accessed from this shard's thread, so they are
    /// stored separately from the shared state for the page that can be
    /// accessed concurrently, to minimize false sharing.
    local: Box<[page::Local]>,
    /// The shared state for each page in this shard.
    ///
    /// This consists of the page's metadata (size, previous size), remote free
    /// list, and a pointer to the actual array backing that page.
    shared: Box<[page::Shared<T, C>]>,
}

impl<T, C> Shard<T, C>
where
    C: cfg::Config,
{
    #[inline(always)]
    pub(crate) fn get<U>(
        &self,
        idx: usize,
        f: impl FnOnce(&T) -> &U,
    ) -> Option<page::slot::Guard<'_, U, C>> {
        debug_assert_eq!(Tid::<C>::from_packed(idx).as_usize(), self.tid);
        let (addr, page_index) = page::indices::<C>(idx);

        test_println!("-> {:?}", addr);
        if page_index > self.shared.len() {
            return None;
        }

        self.shared[page_index].get(addr, idx, f)
    }

    pub(crate) fn new(tid: usize) -> Self {
        let mut total_sz = 0;
        let shared = (0..C::MAX_PAGES)
            .map(|page_num| {
                let sz = C::page_size(page_num);
                let prev_sz = total_sz;
                total_sz += sz;
                page::Shared::new(sz, prev_sz)
            })
            .collect();
        let local = (0..C::MAX_PAGES).map(|_| page::Local::new()).collect();
        Self { tid, local, shared }
    }
}

impl<T, C> Shard<Option<T>, C>
where
    C: cfg::Config,
{
    /// Remove an item on the shard's local thread.
    pub(crate) fn take_local(&self, idx: usize) -> Option<T> {
        debug_assert_eq!(Tid::<C>::from_packed(idx).as_usize(), self.tid);
        let (addr, page_index) = page::indices::<C>(idx);

        test_println!("-> remove_local {:?}", addr);

        self.shared
            .get(page_index)?
            .take(addr, C::unpack_gen(idx), self.local(page_index))
    }

    /// Remove an item, while on a different thread from the shard's local thread.
    pub(crate) fn take_remote(&self, idx: usize) -> Option<T> {
        debug_assert_eq!(Tid::<C>::from_packed(idx).as_usize(), self.tid);
        debug_assert!(Tid::<C>::current().as_usize() != self.tid);

        let (addr, page_index) = page::indices::<C>(idx);

        test_println!("-> take_remote {:?}; page {:?}", addr, page_index);

        let shared = self.shared.get(page_index)?;
        shared.take(addr, C::unpack_gen(idx), shared.free_list())
    }

    pub(crate) fn remove_local(&self, idx: usize) -> bool {
        debug_assert_eq!(Tid::<C>::from_packed(idx).as_usize(), self.tid);
        let (addr, page_index) = page::indices::<C>(idx);

        if page_index > self.shared.len() {
            return false;
        }

        self.shared[page_index].remove(addr, C::unpack_gen(idx), self.local(page_index))
    }

    pub(crate) fn remove_remote(&self, idx: usize) -> bool {
        debug_assert_eq!(Tid::<C>::from_packed(idx).as_usize(), self.tid);
        let (addr, page_index) = page::indices::<C>(idx);

        if page_index > self.shared.len() {
            return false;
        }

        let shared = &self.shared[page_index];
        shared.remove(addr, C::unpack_gen(idx), shared.free_list())
    }

    pub(crate) fn iter<'a>(&'a self) -> std::slice::Iter<'a, page::Shared<Option<T>, C>> {
        self.shared.iter()
    }
}

impl<T, C> Shard<T, C>
where
    T: Clear + Default,
    C: cfg::Config,
{
    pub(crate) fn init_with<F>(&self, mut func: F) -> Option<usize>
    where
        F: FnMut(&page::slot::Slot<T, C>) -> Option<page::slot::Generation<C>>,
    {
        // Can we fit the value into an existing page?
        for (page_idx, page) in self.shared.iter().enumerate() {
            let local = self.local(page_idx);

            test_println!("-> page {}; {:?}; {:?}", page_idx, local, page);

            if let Some(poff) = page.init_with(local, &mut func) {
                return Some(poff);
            }
        }

        None
    }

    pub(crate) fn mark_clear_local(&self, idx: usize) -> bool {
        debug_assert_eq!(Tid::<C>::from_packed(idx).as_usize(), self.tid);
        let (addr, page_index) = page::indices::<C>(idx);

        if page_index > self.shared.len() {
            return false;
        }

        self.shared[page_index].mark_clear(addr, C::unpack_gen(idx), self.local(page_index))
    }

    pub(crate) fn mark_clear_remote(&self, idx: usize) -> bool {
        debug_assert_eq!(Tid::<C>::from_packed(idx).as_usize(), self.tid);
        let (addr, page_index) = page::indices::<C>(idx);

        if page_index > self.shared.len() {
            return false;
        }

        let shared = &self.shared[page_index];
        shared.mark_clear(addr, C::unpack_gen(idx), shared.free_list())
    }

    #[inline(always)]
    fn local(&self, i: usize) -> &page::Local {
        #[cfg(debug_assertions)]
        debug_assert_eq!(
            Tid::<C>::current().as_usize(),
            self.tid,
            "tried to access local data from another thread!"
        );

        &self.local[i]
    }
}

impl<T: fmt::Debug, C: cfg::Config> fmt::Debug for Shard<T, C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let mut d = f.debug_struct("Shard");

        #[cfg(debug_assertions)]
        d.field("tid", &self.tid);
        d.field("shared", &self.shared).finish()
    }
}
