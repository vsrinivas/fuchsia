// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utility types to handle shared contexts.

use futures::{
    lock::{Mutex, MutexGuard},
    Future, FutureExt,
};
use std::ops::{Deref, DerefMut};

/// A structure that holds an internal value of type `T`.
///
/// `InnerValue` is equivalent to `AsRef + AsMut`, that is, it provides
/// references (mutable and not) to internal values through its
/// [`InnerValue::inner`] and [`InnerValue::inner_mut`] methods.
///
/// `InnerValue<T>` is implemented for all `T`, that is, all types can get a
/// reference to themselves.
///
/// An equivalent operation to `InnerValue` is provided by [`FromContainer`],
/// much like the standard `From` and `Into` traits.
pub(crate) trait InnerValue<T> {
    /// Retrieves an immutable reference to the internal `T`.
    fn inner(&self) -> &T;
    /// Retrieves a mutable reference to the internal `T`.
    fn inner_mut(&mut self) -> &mut T;
}

impl<T> InnerValue<T> for T {
    fn inner(&self) -> &T {
        self
    }

    fn inner_mut(&mut self) -> &mut T {
        self
    }
}

/// A helper trait to disambiguate types that implement multiple versions of
/// `InnerValue` with some syntatic sugar.
pub(crate) trait MultiInnerValue {
    /// Retrieves an immutable reference to an internal `T`, if `self`
    /// implements `InnerValue<T>`.
    fn get_inner<T>(&self) -> &T
    where
        Self: InnerValue<T>,
    {
        InnerValue::<T>::inner(self)
    }

    /// Retrieves a mutable reference to an internal `T`, if `self`
    /// implements `InnerValue<T>`.
    fn get_inner_mut<T>(&mut self) -> &mut T
    where
        Self: InnerValue<T>,
    {
        InnerValue::<T>::inner_mut(self)
    }
}

/// `MultiInnerValue` is implemented for all types, since all its trait `fn`s
/// have specific trait bounds.
impl<T> MultiInnerValue for T {}

/// The equivalent "inverse" of [`InnerValue`].
///
/// [`FromOuterValue`] allows to get a reference to `Self` if a reference to a
/// type `T` that contains a reference to `Self` is provided.
///
/// [`FromOuterValue`] is automatically provided to all the types for which
/// their containers define an `InnerValue` impl.
pub(crate) trait FromOuterValue<T> {
    /// Retrieves an immutable reference to `Self` from the `outer` container.
    fn from_outer(outer: &T) -> &Self;
    /// Retrieves a mutable reference to `Self` from the `outer` container.
    fn from_outer_mut(outer: &mut T) -> &mut Self;
}

impl<V, O> FromOuterValue<O> for V
where
    O: InnerValue<V>,
{
    fn from_outer(outer: &O) -> &Self {
        outer.inner()
    }

    fn from_outer_mut(outer: &mut O) -> &mut Self {
        outer.inner_mut()
    }
}

/// An RAII object that holds a lock on some structure `G` and gives access to
/// its internal field `T` as its `Deref` target.
///
/// `T` is some type whose reference can be retrieved from `G`.
pub(crate) struct LockedGuardContext<'a, G, T> {
    guard: MutexGuard<'a, G>,
    _marker: std::marker::PhantomData<T>,
}

impl<'a, G, T> Deref for LockedGuardContext<'a, G, T>
where
    G: InnerValue<T>,
{
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self.guard.deref().inner()
    }
}

impl<'a, G, T> DerefMut for LockedGuardContext<'a, G, T>
where
    G: InnerValue<T>,
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.guard.deref_mut().inner_mut()
    }
}

impl<'a, G, T> LockedGuardContext<'a, G, T> {
    /// Creates a new `LockedContext` with the provided [`MutexGuard`].
    fn new(guard: MutexGuard<'a, G>) -> Self {
        Self { guard, _marker: std::marker::PhantomData }
    }
}

/// A shorthand definition for the type of [`LockedGuardContext`] provided by an
/// implementation `C` of [`LockableContext<T>`].
///
/// [`LockableContext<T>`]: LockableContext
pub(crate) type LockedContext<'a, C, T> =
    LockedGuardContext<'a, <C as LockableContext<'a, T>>::Guard, T>;

/// A type that provides futures-enabled locks to some type `T`.
pub(crate) trait LockableContext<'a, T>
where
    T: Send + Sync + 'static,
{
    /// The type of the guard used in the [`LockedGuardContext`] that is created
    /// by the future in [`LockableContext::lock`].
    type Guard: Send + Sync + 'static + InnerValue<T>;
    /// The future returned by [`LockableContext::lock`]. The `Output` of the
    /// provided `Future` must be an RAII [`LockedGuardContext`] instance.
    type Fut: Future<Output = LockedContext<'a, Self, T>> + Send;

    /// Returns a future that, when completed, retains a lock on a shared
    /// context defined by `Guard`.
    fn lock(&'a self) -> Self::Fut;
}

/// Helper trait to provide implementations of [`LockableContext`] with a target
/// type `T`.
///
/// Implementers of `GuardContext<T>` receive an implementation of
/// `LockableContext<T>` with the same `Guard`.
///
/// This trait should *not* be used as a trait bound, users should use the
/// [`LockableContext`] trait for trait bounds and use `GuardContext` only as a
/// helper to provide a common implementation of [`LockableContext`] over
/// [`futures::lock::Mutex`].
pub(crate) trait GuardContext<T: Send + Sync + 'static>:
    AsRef<Mutex<<Self as GuardContext<T>>::Guard>>
{
    /// The type of the guard used in the [`LockedGuardContext`] that will be
    /// provided by the [`LockableContext`] implementation.
    type Guard: InnerValue<T> + Send + Sync + 'static;
}

impl<'a, G, T, C> LockableContext<'a, T> for C
where
    C: GuardContext<T, Guard = G>,
    G: InnerValue<T> + Send + Sync + 'static,
    T: Send + Sync + 'static,
{
    type Guard = G;
    type Fut = futures::future::Map<
        futures::lock::MutexLockFuture<'a, G>,
        fn(futures::lock::MutexGuard<'a, G>) -> LockedGuardContext<'a, G, T>,
    >;

    fn lock(&'a self) -> Self::Fut {
        AsRef::<Mutex<G>>::as_ref(self).lock().map(LockedGuardContext::new)
    }
}
