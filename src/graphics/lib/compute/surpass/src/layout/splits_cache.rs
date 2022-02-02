// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// IMPORTANT: Upon any code-related modification to this file, please ensure that all commented-out
//            tests that start with `fails_` actually fail to compile *independently* from one
//            another.

use std::{fmt, mem, ptr};

// `SplitsCache` is virtually identical to a `Vec<Range<usize>>` whose range are statically
// guaranteed not to overlap or overflow the initial `slice` that the object has been made with.
//
// This is achieved by forcing the user to produce `&mut [Sealed<T>]` slices in a closure provided
// to the constructor without being able to produce `Sealed<T>` types. By being forced to return
// them inside the `Fn`, this ensure that slices coming from other places other than the arguments
// of the closure are impossible to return, the only exceltion being the `'static` `&mut []` which
// is accounted for.
//
// In order to make sure that the slice we're being given is the same as the one that we built the
// cache from, accessing compares it to the stored `self.ptr` and `self.len` values.

/// Sealed un-constructible wrapper type used by [`SplitsCache`].
///
/// # Safety
///
/// While this type is `#[repr(transparent)]`, it is **still** undefined-behavior to transmute `T`
/// to [`Sealed<T>`].
#[repr(transparent)]
#[derive(Debug)]
pub struct Sealed<T>(T);

/// A cache of non-overlapping mutable sub-slices of an initial mutable slice that enforces
/// lifetimes dynamically.
///
/// This type is useful when you have to give up on the mutable reference to the slice but need
/// a way to cache them, since indexing with `Range<usize>` would not let you split the mutable
/// slice since [`std::ops::IndexMut::index_mut`] is defined on `&mut self.`
///
/// # Examples
///
/// ```
/// # use surpass::layout::SplitsCache;
/// let mut array = [1, 2, 3];
///
/// let mut splits = SplitsCache::new(|slice| {
///     let (left, right) = slice.split_at_mut(1);
///     Box::new([left, right])
/// });
///
/// for slice in splits.access(&mut array) {
///     for val in slice.iter_mut() {
///         *val += 1;
///     }
/// }
///
/// assert_eq!(array, [2, 3, 4]);
/// ```
pub struct SplitsCache<T> {
    ptr: *mut T,
    len: usize,
    splits_raw: Option<*mut [()]>,
    #[allow(clippy::type_complexity)]
    f: Box<dyn Fn(&mut [Sealed<T>]) -> Box<[&mut [Sealed<T>]]>>,
}

impl<T> SplitsCache<T> {
    /// Creates a new splits cache, storing the splitting logic `f` for further use.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::layout::SplitsCache;
    /// let mut splits = SplitsCache::<()>::new(|slice| {
    ///     let (left, right) = slice.split_at_mut(1);
    ///     // All returned sub-slices should stem from the slice passed above
    ///     Box::new([left, right])
    /// });
    /// ```
    #[inline]
    pub fn new<F>(f: F) -> Self
    where
        F: Fn(&mut [Sealed<T>]) -> Box<[&mut [Sealed<T>]]> + 'static,
    {
        Self { ptr: ptr::null_mut(), len: 0, splits_raw: None, f: Box::new(f) }
    }

    /// Accesses the `slice` by returning all the sub-slices created previously in the closure `f`
    /// passed to [`SplitsCache::new`].
    ///
    /// If the `slice` does not point to the exact same area as the one passed to
    /// [`SplitsCache::new`], this function returns `None`.
    ///
    /// # Examples
    ///
    /// ```
    /// # use surpass::layout::SplitsCache;
    /// let mut array = [1, 2, 3];
    ///
    /// let mut splits = SplitsCache::new(|slice| {
    ///     Box::new([&mut slice[1..]])
    /// });
    ///
    /// let mut copy = array;
    /// let skipped_one = splits.access(&mut array);
    ///
    /// assert_eq!(skipped_one, &mut [&mut copy[1..]]);
    /// ```
    #[inline]
    pub fn access<'c, 's>(&'c mut self, slice: &'s mut [T]) -> &'c mut [&'s mut [T]] {
        if slice.as_mut_ptr() != self.ptr || slice.len() != self.len {
            self.ptr = slice.as_mut_ptr();
            self.len = slice.len();

            if let Some(splits_raw) = self.splits_raw.take() {
                // `self.splits_raw` is a valid fat pointer that can only be created in
                // `SplitsCache::new` from a `Box<[&mut [Sealed<T>]]>`, where `Sealed<T>` is
                // `#[repr(transparent)]` over `T`.
                //
                // The pointer is also still alive since it is always leaked apart from the `Drop`
                // implementation.
                unsafe { Box::from_raw(splits_raw as *mut [&mut [Sealed<T>]]) };
            }
        }

        // TODO: to remove with 2021 edition
        let f = &self.f;
        let splits_raw = *self.splits_raw.get_or_insert_with(|| {
            // Transmuting between `&mut [T]` and `&mut [Sealed<T>]` is safe because `Sealed<T>`
            // is `#[repr(transparent)]`.
            Box::into_raw(f(unsafe { mem::transmute(slice) })) as *mut _
        });

        // `self.splits_raw` is a valid fat pointer that can only be created in
        // `SplitsCache::new` from a `Box<[&mut [Sealed<T>]]>`, where `Sealed<T>` is
        // `#[repr(transparent)]` over `T`.
        //
        // The pointer is also still alive since it is always leaked apart from the `Drop`
        // implementation.
        //
        // Since the data for the actual slices (the `(*mut T, usize)` pair) lives in the
        // cache, we need to differentiate between lifetimes. This means that the slice of
        // slices get lifetime `'c` (from `self`) and the slice that point to the data get
        // lifetime `'s` (from `slice`).
        let slices: Box<[&mut [T]]> = unsafe { Box::from_raw(splits_raw as *mut _) };
        Box::leak(slices)
    }
}

impl<T> Drop for SplitsCache<T> {
    fn drop(&mut self) {
        if let Some(splits_raw) = self.splits_raw {
            // `self.splits_raw` is a valid fat pointer that can only be created in
            // `SplitsCache::new` from a `Box<[&mut [Sealed<T>]]>`, where `Sealed<T>` is
            // `#[repr(transparent)]` over `T`.
            //
            // The pointer is also still alive since it is always leaked apart from the `Drop`
            // implementation.
            unsafe { Box::from_raw(splits_raw as *mut [&mut [Sealed<T>]]) };
        }
    }
}

impl<T> fmt::Debug for SplitsCache<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let ranges = unsafe {
            let slices: Box<[&mut [T]]> = if let Some(splits_raw) = self.splits_raw {
                // `self.splits_raw` is a valid fat pointer that can only be created in
                // `SplitsCache::new` from a `Box<[&mut [Sealed<T>]]>`, where `Sealed<T>` is
                // `#[repr(transparent)]` over `T`.
                //
                // The pointer is also still alive since it is always leaked apart from the `Drop`
                // implementation.
                Box::from_raw(splits_raw as *mut _)
            } else {
                Box::new([])
            };

            Box::leak(slices).iter().map(|slice| {
                if slice.is_empty() {
                    return [0..0];
                }

                // Slices saved in `self.splits_raw` can only derive from `self.ptr` or the static
                // `&mut []` which is dealt with above.
                let start = slice.as_ptr().offset_from(self.ptr) as usize;

                [start..start + slice.len()]
            })
        };

        f.debug_list().entries(ranges).finish()
    }
}

// `SplitsCache` is equivalent to a `Vec<Range<usize>>` which itself is `Send`.
unsafe impl<T> Send for SplitsCache<T> {}
// `SplitsCache` is equivalent to a `Vec<Range<usize>>` which itself is `Sync`.
unsafe impl<T> Sync for SplitsCache<T> {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn correct_use() {
        let mut array = [1, 2, 3];

        let mut splits = SplitsCache::new(|slice| {
            let (left, right) = slice.split_at_mut(1);
            Box::new([left, right])
        });

        for slice in splits.access(&mut array) {
            for val in slice.iter_mut() {
                *val += 1;
            }
        }

        assert_eq!(array, [2, 3, 4]);
    }

    #[test]
    fn different_arrays() {
        let mut array0 = [1, 2, 3];
        let mut array1 = [1, 2, 3];

        let mut splits = SplitsCache::new(|slice| {
            let (left, right) = slice.split_at_mut(1);
            Box::new([left, right])
        });

        for slice in splits.access(&mut array0) {
            for val in slice.iter_mut() {
                *val += 1;
            }
        }

        for slice in splits.access(&mut array1) {
            for val in slice.iter_mut() {
                *val += 1;
            }
        }

        assert_eq!(array0, [2, 3, 4]);
        assert_eq!(array1, [2, 3, 4]);
    }

    #[test]
    fn par_iter() {
        use rayon::prelude::*;

        let mut array = [1, 2, 3];

        let mut splits = SplitsCache::new(|slice| {
            let (left, right) = slice.split_at_mut(1);
            Box::new([left, right])
        });

        splits.access(&mut array).par_iter_mut().for_each(|slice| {
            for val in slice.iter_mut() {
                *val += 1;
            }
        });

        assert_eq!(array, [2, 3, 4]);
    }

    #[test]
    fn works_with_static() {
        let mut array = [1, 2, 3];

        let mut splits = SplitsCache::new(|slice| {
            let (left, right) = slice.split_at_mut(1);
            Box::new([left, right, &mut []])
        });

        for slice in splits.access(&mut array) {
            for val in slice.iter_mut() {
                *val += 1;
            }
        }

        assert_eq!(array, [2, 3, 4]);
    }

    // #[test]
    // fn fails_on_not_send() {
    //     use std::rc::Rc;

    //     use rayon::prelude::*;

    //     let mut array = [Rc::new(1), Rc::new(2), Rc::new(3)];

    //     let mut splits = SplitsCache::new(|slice| {
    //         let (left, right) = slice.split_at_mut(1);
    //         Box::new([left, right])
    //     });

    //     splits.access(&mut array).par_iter_mut().for_each(|slice| {
    //         for val in slice.iter_mut() {
    //             *val += 1;
    //         }
    //     });
    // }

    // #[test]
    // fn fails_on_wrong_array() {
    //     let mut array = [1, 2, 3];

    //     let mut splits = SplitsCache::new(|_| {
    //         Box::new([&mut array[..]])
    //     });
    // }

    // #[test]
    // fn fails_on_exporting_wrapped() {
    //     let mut leaked = None;

    //     let mut splits0 = SplitsCache::<()>::new(|slice| {
    //         leaked = Some(slice);
    //         Box::new([])
    //     });

    //     let mut splits1 = SplitsCache::<()>::new(|slice| {
    //         Box::new([leaked.take().unwrap()])
    //     });
    // }

    // #[test]
    // fn fails_on_drop_slice() {
    //     let mut array = [1, 2, 3];

    //     let mut splits = SplitsCache::new(|slice| {
    //         let (left, right) = slice.split_at_mut(1);
    //         Box::new([left, right])
    //     });

    //     let slices = splits.access(&mut array);

    //     std::mem::drop(array);

    //     slices[0][0] = 0;
    // }
}
