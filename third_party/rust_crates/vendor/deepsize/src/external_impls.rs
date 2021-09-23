#[cfg(feature = "slotmap")]
mod slotmap_impl {
    use crate::{known_deep_size, Context, DeepSizeOf};
    use core::mem::size_of;

    known_deep_size!(0; slotmap::KeyData, slotmap::DefaultKey);

    impl<K, V> DeepSizeOf for slotmap::SlotMap<K, V>
    where
        K: DeepSizeOf + slotmap::Key,
        V: DeepSizeOf + slotmap::Slottable,
    {
        fn deep_size_of_children(&self, context: &mut Context) -> usize {
            self.iter().fold(0, |sum, (key, val)| {
                sum + key.deep_size_of_children(context) + val.deep_size_of_children(context)
            }) + self.capacity() * size_of::<(u32, V)>()
        }
    }
}

#[cfg(feature = "slab")]
mod slab_impl {
    use crate::{Context, DeepSizeOf};
    use core::mem::size_of;

    // Mirror's `slab`'s internal `Entry` struct
    enum MockEntry<T> {
        _Vacant(usize),
        _Occupied(T),
    }

    impl<T> DeepSizeOf for slab::Slab<T>
    where
        T: DeepSizeOf,
    {
        fn deep_size_of_children(&self, context: &mut Context) -> usize {
            let capacity_size = self.capacity() * size_of::<MockEntry<T>>();
            let owned_size = self
                .iter()
                .fold(0, |sum, (_, val)| sum + val.deep_size_of_children(context));
            capacity_size + owned_size
        }
    }
}

#[cfg(feature = "arrayvec")]
mod arrayvec_impl {
    use crate::{known_deep_size, Context, DeepSizeOf};

    impl<A> DeepSizeOf for arrayvec::ArrayVec<A>
    where
        A: arrayvec::Array,
        <A as arrayvec::Array>::Item: DeepSizeOf,
    {
        fn deep_size_of_children(&self, context: &mut Context) -> usize {
            self.iter()
                .fold(0, |sum, elem| sum + elem.deep_size_of_children(context))
        }
    }

    known_deep_size!(0; { A: arrayvec::Array<Item=u8> + Copy } arrayvec::ArrayString<A>);
}

#[cfg(feature = "smallvec")]
mod smallvec_impl {
    use crate::{Context, DeepSizeOf};
    use core::mem::size_of;

    impl<A> DeepSizeOf for smallvec::SmallVec<A>
    where
        A: smallvec::Array,
        <A as smallvec::Array>::Item: DeepSizeOf,
    {
        fn deep_size_of_children(&self, context: &mut Context) -> usize {
            let child_size = self
                .iter()
                .fold(0, |sum, elem| sum + elem.deep_size_of_children(context));
            if self.spilled() {
                child_size + self.capacity() * size_of::<<A as smallvec::Array>::Item>()
            } else {
                child_size
            }
        }
    }
}

#[cfg(feature = "hashbrown")]
mod hashbrown_impl {
    use crate::{Context, DeepSizeOf};
    use core::mem::size_of;

    // This is probably still incorrect, but it's better than before
    impl<K, V, S> DeepSizeOf for hashbrown::HashMap<K, V, S>
    where
        K: DeepSizeOf + Eq + std::hash::Hash,
        V: DeepSizeOf,
        S: std::hash::BuildHasher,
    {
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

    impl<K, S> DeepSizeOf for hashbrown::HashSet<K, S>
    where
        K: DeepSizeOf + Eq + std::hash::Hash,
        S: std::hash::BuildHasher,
    {
        fn deep_size_of_children(&self, context: &mut Context) -> usize {
            self.iter()
                .fold(0, |sum, key| sum + key.deep_size_of_children(context))
                + self.capacity() * size_of::<K>()
        }
    }
}

#[cfg(feature = "indexmap")]
mod indexmap_impl {
    use crate::{Context, DeepSizeOf};
    use core::mem::size_of;
    use indexmap::{IndexMap, IndexSet};

    // IndexMap uses a vec of buckets (usize, K, V) as backing, with
    // a hashbrown::RawTable<usize> for lookups.  This method will
    // consistently underestimate, because IndexMap::capacity will
    // return the min of the capacity of the buckets list and the
    // capacity of the raw table.
    impl<K, V, S> DeepSizeOf for IndexMap<K, V, S>
        where K: DeepSizeOf, V: DeepSizeOf
    {
        fn deep_size_of_children(&self, context: &mut Context) -> usize {
            let child_sizes = self.iter().fold(0, |sum, (key, val)| {
                sum + key.deep_size_of_children(context) + val.deep_size_of_children(context)
            });
            let map_size = self.capacity() * (size_of::<(usize, K, V)>() + size_of::<usize>());
            child_sizes + map_size
        }
    }
    impl<K, S> DeepSizeOf for IndexSet<K, S>
        where K: DeepSizeOf
    {
        fn deep_size_of_children(&self, context: &mut Context) -> usize {
            let child_sizes = self.iter().fold(0, |sum, key| {
                sum + key.deep_size_of_children(context)
            });
            let map_size = self.capacity() * (size_of::<(usize, K, ())>() + size_of::<usize>());
            child_sizes + map_size
        }
    }
}

#[cfg(feature = "chrono")]
mod chrono_impl {
    use crate::known_deep_size;
    use chrono::*;

    known_deep_size!(0;
        NaiveDate, NaiveTime, NaiveDateTime, IsoWeek,
        Duration, Month, Weekday,
        FixedOffset, Local, Utc,
        {T: TimeZone} DateTime<T>, {T: TimeZone} Date<T>,
    );
}
