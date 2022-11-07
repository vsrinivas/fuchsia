use crate::{Context, DeepSizeOf};

/// A macro to generate an impl for types with known inner allocation sizes.
///
/// Repurposed from the `heapsize` crate
///
/// Usage:
/// ```rust
/// # #[macro_use] extern crate deepsize; fn main() {
/// struct A(u32);
/// struct B(A, char);
/// struct C(Box<u32>);
///
/// known_deep_size!(0; A, B); // A and B do not have any allocation
/// known_deep_size!(4; C); // C will always have an allocation of 4 bytes
/// # }
/// ```
#[macro_export]
macro_rules! known_deep_size (
    ($size:expr; $($({$($gen:tt)*})? $type:ty,)+) => (
        known_deep_size!($size; $($({$($gen)*})? $type),*);
    );
    ($size:expr; $($({$($gen:tt)*})? $type:ty),+) => (
        $(
            impl$(<$($gen)*>)? $crate::DeepSizeOf for $type {
                #[inline(always)]
                fn deep_size_of_children(&self, _: &mut $crate::Context) -> usize {
                    $size
                }
            }
        )+
    );
);

use core::num;
use core::sync::atomic;

known_deep_size!(0;
    (), bool, char, str,
    u8, u16, u32, u64, u128, usize,
    i8, i16, i32, i64, i128, isize,
    f32, f64,
);
known_deep_size!(0;
    atomic::AtomicBool,
    atomic::AtomicI8,
    atomic::AtomicI16,
    atomic::AtomicI32,
    atomic::AtomicI64,
    atomic::AtomicIsize,
    atomic::AtomicU8,
    atomic::AtomicU16,
    atomic::AtomicU32,
    atomic::AtomicU64,
    atomic::AtomicUsize,

    num::NonZeroI8,
    num::NonZeroI16,
    num::NonZeroI32,
    num::NonZeroI64,
    num::NonZeroI128,
    num::NonZeroIsize,
    num::NonZeroU8,
    num::NonZeroU16,
    num::NonZeroU32,
    num::NonZeroU64,
    num::NonZeroU128,
    num::NonZeroUsize,
);

known_deep_size!(0;
    {T: ?Sized} core::marker::PhantomData<T>,
    {T} core::mem::MaybeUninit<T>,
    // In theory this could be incorrect, but it's unlikely
    {T: Copy} core::cell::Cell<T>,

    // Weak reference counted pointers do not own their contents
    {T} alloc::sync::Weak<T>,
    {T} alloc::rc::Weak<T>,
);

#[cfg(feature = "std")]
mod strings {
    use super::{DeepSizeOf, Context};
    use std::ffi::{OsString, OsStr, CString, CStr};
    use std::path::{PathBuf, Path};

    known_deep_size!(0; Path, OsStr, CStr);

    impl DeepSizeOf for PathBuf {
        fn deep_size_of_children(&self, _: &mut Context) -> usize {
            self.capacity()
        }
    }
    impl DeepSizeOf for OsString {
        fn deep_size_of_children(&self, _: &mut Context) -> usize {
            self.capacity()
        }
    }
    impl DeepSizeOf for CString {
        fn deep_size_of_children(&self, _: &mut Context) -> usize {
            // This may cause a length check at runtime, but that
            // doesn't seem avoidable.  This assumes that the allocation
            // is the exact length of the string and the added null
            // terminator.
            self.as_bytes().len() + 1
        }
    }
}


impl DeepSizeOf for alloc::string::String {
    fn deep_size_of_children(&self, _: &mut Context) -> usize {
        self.capacity()
    }
}

impl<T: DeepSizeOf> DeepSizeOf for core::option::Option<T> {
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        match &self {
            Some(t) => t.deep_size_of_children(context),
            None => 0,
        }
    }
}

impl<R: DeepSizeOf, E: DeepSizeOf> DeepSizeOf for core::result::Result<R, E> {
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        match &self {
            Ok(r) => r.deep_size_of_children(context),
            Err(e) => e.deep_size_of_children(context),
        }
    }
}

impl<T: DeepSizeOf> DeepSizeOf for core::cell::RefCell<T> {
    fn deep_size_of_children(&self, context: &mut Context) -> usize {
        self.borrow().deep_size_of_children(context)
    }
}

#[cfg(feature = "std")]
mod std_sync {
    use crate::{Context, DeepSizeOf};

    impl<T: DeepSizeOf> DeepSizeOf for std::sync::Mutex<T> {
        /// This locks the `Mutex`, so it may deadlock; If the mutex is
        /// poisoned, this returns 0
        fn deep_size_of_children(&self, context: &mut Context) -> usize {
            self.lock()
                .map(|s| s.deep_size_of_children(context))
                .unwrap_or(0)
        }
    }

    impl<T: DeepSizeOf> DeepSizeOf for std::sync::RwLock<T> {
        /// This reads the `RwLock`, so it may deadlock; If the lock is
        /// poisoned, this returns 0
        fn deep_size_of_children(&self, context: &mut Context) -> usize {
            self.read()
                .map(|s| s.deep_size_of_children(context))
                .unwrap_or(0)
        }
    }
}

macro_rules! deep_size_array {
    ($($num:expr,)+) => {
        deep_size_array!($($num),+);
    };
    ($($num:expr),+) => {
        $(
            impl<T: DeepSizeOf> DeepSizeOf for [T; $num] {
                fn deep_size_of_children(&self, context: &mut Context) -> usize {
                    self.as_ref().deep_size_of_children(context)
                }
            }
        )+
    };
}

// Can't wait for const generics
// A year and a half later, still waiting
deep_size_array!(
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30, 31, 32
);

macro_rules! deep_size_tuple {
    ($(($n:tt, $T:ident)),+ ) => {
        impl<$($T,)+> DeepSizeOf for ($($T,)+)
            where $($T: DeepSizeOf,)+
        {
            fn deep_size_of_children(&self, context: &mut Context) -> usize {
                0 $( + self.$n.deep_size_of_children(context))+
            }
        }
    };
}

deep_size_tuple!((0, A));
deep_size_tuple!((0, A), (1, B));
deep_size_tuple!((0, A), (1, B), (2, C));
deep_size_tuple!((0, A), (1, B), (2, C), (3, D));
deep_size_tuple!((0, A), (1, B), (2, C), (3, D), (4, E));
deep_size_tuple!((0, A), (1, B), (2, C), (3, D), (4, E), (5, F));
deep_size_tuple!((0, A), (1, B), (2, C), (3, D), (4, E), (5, F), (6, G));
deep_size_tuple!(
    (0, A),
    (1, B),
    (2, C),
    (3, D),
    (4, E),
    (5, F),
    (6, G),
    (7, H)
);
deep_size_tuple!(
    (0, A),
    (1, B),
    (2, C),
    (3, D),
    (4, E),
    (5, F),
    (6, G),
    (7, H),
    (8, I)
);
deep_size_tuple!(
    (0, A),
    (1, B),
    (2, C),
    (3, D),
    (4, E),
    (5, F),
    (6, G),
    (7, H),
    (8, I),
    (9, J)
);
