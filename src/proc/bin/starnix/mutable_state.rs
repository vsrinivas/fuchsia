// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Macros used with struct containing an immutable state and a RwLock to a mutable state.
//!
//! These macros define a new type of read and write guards that allow access to both the
//! mutable and immutable state. To use it, one must:
//! - Define the main struct (e.g. `Foo`) for the object with the immutable state.
//! - Define a struct for the mutable state (e.g. `FooMutableState`).
//! - Have a RwLock<> in the main struct for the mutable state (e.g. `mutable_state:
//! RwLock<FooMutableState>`).
//! - In the implementation of the main struct, add a call to the `state_accessor` macros:
//! ```
//! impl Foo {
//!   state_accessor!(Foo, mutable_state);
//! }
//! ```
//! - Write the method on the guards using the state_implementation macro:
//! ```
//! state_implementation!(Foo, FooMutableState, {
//!     // Some comment
//!     fn do_something(&self) -> i32 {
//!         0
//!     }
//! });
//! ```
//!
//! # Complete example:
//!
//! ```
//! pub struct FooMutableState {
//!     y: i32,
//! }

//! pub struct Foo {
//!     x: i32,
//!     mutable_state: RwLock<FooMutableState>,
//! }

//! impl Foo {
//!     fn new() -> Self {
//!         Self {
//!             x: 2,
//!             mutable_state: RwLock::new(FooMutableState { y: 3 }),
//!         }
//!     }

//!     state_accessor!(Foo, mutable_state);
//! }

//! state_implementation!(Foo, FooMutableState, {
//!     // Some comment
//!     fn x_and_y(&self) -> i32 {
//!         self.base().x + self.y
//!     }
//!     /// Some rustdoc.
//!     pub fn pub_x_and_y(&self) -> i32 {
//!         self.x_and_y()
//!     }
//!     fn do_something(&self) {}
//!     fn set_y(&mut self, other_y: i32) {
//!         self.y = other_y;
//!     }
//!     pub fn pub_set_y(&mut self, other_y: i32) {
//!         self.set_y(other_y)
//!     }
//!     fn do_something_mutable(&mut self) {
//!         self.do_something();
//!     }
//!     pub fn with_lifecycle<'a>(&self, _n: &'a u32) {}
//!     pub fn with_type<T>(&self, _n: &T) {}
//!     pub fn with_lifecycle_and_type<'a, T>(&self, _n: &'a T) {}
//!     pub fn with_lifecycle_on_self<'a, T>(&'a self, _n: &'a T) {}
//! });
//! ```
//!
//! # Generated code
//!
//! ```
//! pub struct FooMutableState {
//!     y: i32,
//! }
//! pub struct Foo {
//!     x: i32,
//!     mutable_state: RwLock<FooMutableState>,
//! }
//! impl Foo {
//!     fn new() -> Self {
//!         Self {
//!             x: 2,
//!             mutable_state: RwLock::new(FooMutableState { y: 3 }),
//!         }
//!     }
//!     #[allow(dead_code)]
//!     pub fn read<'a>(self: &'a Arc<Foo>) -> FooReadGuardImpl<'a> {
//!         FooReadGuardImpl::new(self, self.mutable_state.read())
//!     }
//!     #[allow(dead_code)]
//!     pub fn write<'a>(self: &'a Arc<Foo>) -> FooWriteGuard<'a> {
//!         FooWriteGuard::new(self, self.mutable_state.write())
//!     }
//! }
//! type FooReadGuardImpl<'guard_lifetime> = ReadableState<'guard_lifetime, Foo, FooMutableState>;
//! pub type FooWriteGuard<'guard_lifetime> = WritableState<'guard_lifetime, Foo, FooMutableState>;
//! pub trait FooReadGuard<'guard_lifetime>:
//!     Baseable<'guard_lifetime, Foo> + std::ops::Deref<Target = FooMutableState>
//! {
//!     /// Some rustdoc.
//!     fn pub_x_and_y(&self) -> i32;
//!     fn with_lifecycle<'a>(&self, _n: &'a u32);
//!     fn with_type<T>(&self, _n: &T);
//!     fn with_lifecycle_and_type<'a, T>(&self, _n: &'a T);
//!     pub fn with_lifecycle_on_self<'a, T>(&'a self, _n: &'a T) {}
//! }
//! impl<'guard_lifetime> FooReadGuard<'guard_lifetime> for FooReadGuardImpl<'guard_lifetime> {
//!     /// Some rustdoc.
//!     fn pub_x_and_y(&self) -> i32 {
//!         self.x_and_y()
//!     }
//!     fn with_lifecycle<'a>(&self, _n: &'a u32) {}
//!     fn with_type<T>(&self, _n: &T) {}
//!     fn with_lifecycle_and_type<'a, T>(&self, _n: &'a T) {}
//! }
//! impl<'guard_lifetime> FooReadGuard<'guard_lifetime> for FooWriteGuard<'guard_lifetime> {
//!     /// Some rustdoc.
//!     fn pub_x_and_y(&self) -> i32 {
//!         self.x_and_y()
//!     }
//!     fn with_lifecycle<'a>(&self, _n: &'a u32) {}
//!     fn with_type<T>(&self, _n: &T) {}
//!     fn with_lifecycle_and_type<'a, T>(&self, _n: &'a T) {}
//!     pub fn with_lifecycle_on_self<'a, T>(&'a self, _n: &'a T) {}
//! }
//! impl<'guard_lifetime> FooReadGuardImpl<'guard_lifetime> {
//!     fn x_and_y(&self) -> i32 {
//!         self.base().x + self.y
//!     }
//!     fn do_something(&self) {}
//! }
//! impl<'guard_lifetime> FooWriteGuard<'guard_lifetime> {
//!     fn x_and_y(&self) -> i32 {
//!         self.base().x + self.y
//!     }
//!     fn do_something(&self) {}
//!     fn set_y(&mut self, other_y: i32) {
//!         self.y = other_y;
//!     }
//!     pub fn pub_set_y(&mut self, other_y: i32) {
//!         self.set_y(other_y)
//!     }
//!     fn do_something_mutable(&mut self) {
//!         self.do_something();
//!     }
//! }
//! ```

use std::ops;
use std::sync::Arc;

use crate::lock::*;

/// Create the read() and write() accessor to respectively the read guard and write guard.
///
/// For a base struct named `Foo`, the read guard will be a trait named `FooReadGuard` and the
/// write guard a struct named `FooWriteGuard`.
macro_rules! state_accessor {
    ($base_name:ident, $field_name:ident) => {
        paste::paste! {
        #[allow(dead_code)]
        pub fn read<'a>(self: &'a Arc<$base_name>) -> [<$base_name ReadGuardImpl>]<'a> {
            [<$base_name ReadGuardImpl>]::new(self, self.$field_name.read())
        }
        #[allow(dead_code)]
        pub fn write<'a>(self: &'a Arc<$base_name>) -> [<$base_name WriteGuard>]<'a> {
            [<$base_name WriteGuard>]::new(self, self.$field_name.write())
        }
        }
    };
}

/// Create the trait and struct for the read and write guards using the methods define inside the
/// macro.
macro_rules! state_implementation {
    ($base_name:ident, $mutable_name: ident, {
        $(
            $tt:tt
        )*
    }) => {
        paste::paste! {
        type [<$base_name ReadGuardImpl>]<'guard_lifetime> = ReadableState<'guard_lifetime,  $base_name,  $mutable_name>;
        pub type [<$base_name WriteGuard>]<'guard_lifetime> = WritableState<'guard_lifetime,  $base_name,  $mutable_name>;

        pub trait [<$base_name ReadGuard>]<'guard_lifetime>: Baseable<'guard_lifetime, $base_name> + std::ops::Deref<Target = $mutable_name>
        {
            filter_methods!(RoPublicMethodSignature, $($tt)*);
        }

        impl<'guard_lifetime> [<$base_name ReadGuard>]<'guard_lifetime> for [<$base_name ReadGuardImpl>]<'guard_lifetime> {
            filter_methods!(RoPublicMethod, $($tt)*);
        }
        impl<'guard_lifetime> [<$base_name ReadGuard>]<'guard_lifetime> for [<$base_name WriteGuard>]<'guard_lifetime> {
            filter_methods!(RoPublicMethod, $($tt)*);
        }

        impl<'guard_lifetime> [<$base_name ReadGuardImpl>]<'guard_lifetime> {
            filter_methods!(RoPrivateMethod, $($tt)*);
        }

        impl<'guard_lifetime> [<$base_name WriteGuard>]<'guard_lifetime> {
            filter_methods!(RoPrivateMethod, $($tt)*);
            filter_methods!(RwMethod, $($tt)*);
        }
        }
    };
}

pub trait Baseable<'a, B> {
    fn base(&self) -> &'a Arc<B>;
}

pub struct ReadableState<'a, B, S> {
    base: &'a Arc<B>,
    state: RwLockReadGuard<'a, S>,
}

impl<'a, B, S> ReadableState<'a, B, S> {
    pub fn new(base: &'a Arc<B>, state: RwLockReadGuard<'a, S>) -> Self {
        Self { base, state }
    }
}

impl<'a, B, S> Baseable<'a, B> for ReadableState<'a, B, S> {
    fn base(&self) -> &'a Arc<B> {
        self.base
    }
}

impl<'a, B, S> ops::Deref for ReadableState<'a, B, S> {
    type Target = S;

    fn deref(&self) -> &Self::Target {
        self.state.deref()
    }
}

pub struct WritableState<'a, B, S> {
    base: &'a Arc<B>,
    state: RwLockWriteGuard<'a, S>,
}

impl<'a, B, S> WritableState<'a, B, S> {
    pub fn new(base: &'a Arc<B>, state: RwLockWriteGuard<'a, S>) -> Self {
        Self { base, state }
    }
}

impl<'a, B, S> Baseable<'a, B> for WritableState<'a, B, S> {
    fn base(&self) -> &'a Arc<B> {
        self.base
    }
}

impl<'a, B, S> ops::Deref for WritableState<'a, B, S> {
    type Target = S;

    fn deref(&self) -> &Self::Target {
        self.state.deref()
    }
}

impl<'a, B, S> ops::DerefMut for WritableState<'a, B, S> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.state.deref_mut()
    }
}

/// This macro matches the methods inside a `state_implementation!` macro depending on their
/// visibility and mutability so that the `state_implementation!` might dispatch these to the right
/// trait or implementation.
macro_rules! filter_methods {
    // No more token.
    ($_:ident, ) => {};
    // Match non mutable, public methods and output their signature.
    (RoPublicMethodSignature, $(#[$meta:meta])* pub fn $fn:ident $(<$($template:tt),*>)? ( & $self_lifetime:lifetime $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        $(#[$meta])* fn $fn $(<$($template),*>)?( & $self_lifetime $self_ $(, $name : $type)* ) $(-> $ret)*;
        filter_methods!(RoPublicMethodSignature, $($tail)*);
    };
    (RoPublicMethodSignature, $(#[$meta:meta])* pub fn $fn:ident $(<$($template:tt),*>)? ( & $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        $(#[$meta])* fn $fn $(<$($template),*>)?( & $self_ $(, $name : $type)* ) $(-> $ret)*;
        filter_methods!(RoPublicMethodSignature, $($tail)*);
    };
    // Match non mutable, public methods and output it.
    (RoPublicMethod, $(#[$meta:meta])* pub fn $fn:ident $(<$($template:tt),*>)? ( & $self_lifetime:lifetime  $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        $(#[$meta])* fn $fn $(<$($template),*>)?( & $self_lifetime $self_ $(, $name : $type)* ) $(-> $ret)* $body
        filter_methods!(RoPublicMethod, $($tail)*);
    };
    (RoPublicMethod, $(#[$meta:meta])* pub fn $fn:ident $(<$($template:tt),*>)? ( & $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        $(#[$meta])* fn $fn $(<$($template),*>)?( & $self_ $(, $name : $type)* ) $(-> $ret)* $body
        filter_methods!(RoPublicMethod, $($tail)*);
    };
    // Match non mutable, private methods and output it.
    (RoPrivateMethod, $(#[$meta:meta])* fn $fn:ident $(<$($template:tt),*>)? ( & $self_lifetime:lifetime  $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        $(#[$meta])* fn $fn $(<$($template),*>)?( & $self_lifetime $self_ $(, $name : $type)* ) $(-> $ret)* $body
        filter_methods!(RoPrivateMethod, $($tail)*);
    };
    (RoPrivateMethod, $(#[$meta:meta])* fn $fn:ident $(<$($template:tt),*>)? ( & $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        $(#[$meta])* fn $fn $(<$($template),*>)?( & $self_ $(, $name : $type)* ) $(-> $ret)* $body
        filter_methods!(RoPrivateMethod, $($tail)*);
    };
    // Match mutable methods and output it.
    (RwMethod, $(#[$meta:meta])* $vis:vis fn $fn:ident $(<$($template:tt),*>)? ( & $self_lifetime:lifetime  mut $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        $(#[$meta])* $vis fn $fn $(<$($template),*>)?( & $self_lifetime mut $self_ $(, $name : $type)* ) $(-> $ret)* $body
        filter_methods!(RwMethod, $($tail)*);
    };
    (RwMethod, $(#[$meta:meta])* $vis:vis fn $fn:ident $(<$($template:tt),*>)? ( & mut $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        $(#[$meta])* $vis fn $fn $(<$($template),*>)?( & mut $self_ $(, $name : $type)* ) $(-> $ret)* $body
        filter_methods!(RwMethod, $($tail)*);
    };
    // Next 4 patterns match every type of method. They are used to remove the tokens associated
    // with a method that has not been match by the previous patterns.
    ($qualifier:ident, $(#[$meta:meta])* $(pub)? fn $fn:ident $(<$($template:tt),*>)? ( & $self_lifetime:lifetime  $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        filter_methods!($qualifier, $($tail)*);
    };
    ($qualifier:ident, $(#[$meta:meta])* $(pub)? fn $fn:ident $(<$($template:tt),*>)? ( & $self_lifetime:lifetime mut $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        filter_methods!($qualifier, $($tail)*);
    };
    ($qualifier:ident, $(#[$meta:meta])* $(pub)? fn $fn:ident $(<$($template:tt),*>)? ( & $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        filter_methods!($qualifier, $($tail)*);
    };
    ($qualifier:ident, $(#[$meta:meta])* $(pub)? fn $fn:ident $(<$($template:tt),*>)? ( &mut $self_:tt $(, $name:ident : $type:ty)* $(,)? ) $(-> $ret:ty)* $body:block $($tail:tt)*) => {
        filter_methods!($qualifier, $($tail)*);
    };
}

// Public re-export of macros allows them to be used like regular rust items.
pub(crate) use filter_methods;
pub(crate) use state_accessor;
pub(crate) use state_implementation;

#[cfg(test)]
mod test {
    use super::*;

    pub struct FooMutableState {
        y: i32,
    }

    pub struct Foo {
        x: i32,
        mutable_state: RwLock<FooMutableState>,
    }

    impl Foo {
        fn new() -> Self {
            Self { x: 2, mutable_state: RwLock::new(FooMutableState { y: 3 }) }
        }

        state_accessor!(Foo, mutable_state);
    }

    state_implementation!(Foo, FooMutableState, {
        // Some comment
        fn x_and_y(&self) -> i32 {
            self.base().x + self.y
        }
        /// Some rustdoc.
        pub fn pub_x_and_y(&self) -> i32 {
            self.x_and_y()
        }
        fn do_something(&self) {}
        fn set_y(&mut self, other_y: i32) {
            self.y = other_y;
        }
        pub fn pub_set_y(&mut self, other_y: i32) {
            self.set_y(other_y)
        }
        fn do_something_mutable(&mut self) {
            self.do_something();
        }

        pub fn with_lifecycle<'a>(&self, _n: &'a u32) {}
        pub fn with_type<T>(&self, _n: &T) {}
        pub fn with_lifecycle_and_type<'a, T>(&self, _n: &'a T) {}
        pub fn with_lifecycle_on_self<'a, T>(&'a self, _n: &'a T) {}
    });

    fn take_foo_state<'a>(foo_state: &impl FooReadGuard<'a>) -> i32 {
        foo_state.pub_x_and_y()
    }

    #[::fuchsia::test]
    fn test_generation() {
        let foo = Arc::new(Foo::new());

        assert_eq!(foo.read().x_and_y(), 5);
        assert_eq!(foo.read().pub_x_and_y(), 5);
        assert_eq!(foo.write().pub_x_and_y(), 5);
        foo.write().set_y(22);
        assert_eq!(foo.read().pub_x_and_y(), 24);
        assert_eq!(foo.write().pub_x_and_y(), 24);
        foo.write().pub_set_y(20);
        assert_eq!(take_foo_state(&foo.read()), 22);
        assert_eq!(take_foo_state(&foo.write()), 22);

        foo.read().do_something();
        foo.write().do_something();
        foo.write().do_something_mutable();
    }
}
