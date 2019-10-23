// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The `Service` trait and its trait-object wrappers.

use {
    fidl::endpoints::{RequestStream, UnifiedServiceRequest},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    std::marker::PhantomData,
};

/// `Service` connects channels to service instances.
///
/// Note that this trait is implemented by the `FidlService` type.
pub trait Service {
    /// The type of the value yielded by the `spawn_service` callback.
    type Output;

    /// Create a new instance of the service on the provided `zx::Channel`.
    ///
    /// The value returned by this function will be yielded from the stream
    /// output of the `ServiceFs` type.
    fn connect(&mut self, channel: zx::Channel) -> Option<Self::Output>;

    /// Create a new instance of the service on the provided `zx::Channel`.
    /// The service is hosted at the given `path`.
    ///
    /// The value returned by this function will be yielded from the stream
    /// output of the `ServiceFs` type.
    fn connect_at(&mut self, _path: &str, channel: zx::Channel) -> Option<Self::Output> {
        self.connect(channel)
    }
}

impl<F, O> Service for F
where
    F: FnMut(zx::Channel) -> Option<O>,
{
    type Output = O;
    fn connect(&mut self, channel: zx::Channel) -> Option<Self::Output> {
        (self)(channel)
    }
}

/// A wrapper for functions from `RequestStream` to `Output` which implements
/// `Service`.
///
/// This type throws away channels that cannot be converted to the appropriate
/// `RequestStream` type.
pub struct FidlService<F, RS, Output>
where
    F: FnMut(RS) -> Output,
    RS: RequestStream,
{
    f: F,
    marker: PhantomData<fn(RS) -> Output>,
}

impl<F, RS, Output> From<F> for FidlService<F, RS, Output>
where
    F: FnMut(RS) -> Output,
    RS: RequestStream,
{
    fn from(f: F) -> Self {
        Self { f, marker: PhantomData }
    }
}

impl<F, RS, Output> Service for FidlService<F, RS, Output>
where
    F: FnMut(RS) -> Output,
    RS: RequestStream,
{
    type Output = Output;
    fn connect(&mut self, channel: zx::Channel) -> Option<Self::Output> {
        match fasync::Channel::from_channel(channel) {
            Ok(chan) => Some((self.f)(RS::from_channel(chan))),
            Err(e) => {
                eprintln!("ServiceFs failed to convert channel to fasync channel: {:?}", e);
                None
            }
        }
    }
}

/// A wrapper for functions from `UnifiedServiceRequest` to `Output` which implements
/// `Service`.
///
/// This type throws away channels that cannot be converted to the appropriate
/// `UnifiedServiceRequest` type.
pub struct FidlServiceMember<F, USR, Output>
where
    F: FnMut(USR) -> Output,
    USR: UnifiedServiceRequest,
{
    f: F,
    marker: PhantomData<fn(USR) -> Output>,
}

impl<F, USR, Output> From<F> for FidlServiceMember<F, USR, Output>
where
    F: FnMut(USR) -> Output,
    USR: UnifiedServiceRequest,
{
    fn from(f: F) -> Self {
        Self { f, marker: PhantomData }
    }
}

impl<F, USR, Output> Service for FidlServiceMember<F, USR, Output>
where
    F: FnMut(USR) -> Output,
    USR: UnifiedServiceRequest,
{
    type Output = Output;

    fn connect(&mut self, _channel: zx::Channel) -> Option<Self::Output> {
        // FidlServiceMember needs to know which member to dispatch to.
        unimplemented!();
    }

    fn connect_at(&mut self, path: &str, channel: zx::Channel) -> Option<Self::Output> {
        match fasync::Channel::from_channel(channel) {
            Ok(chan) => Some((self.f)(USR::dispatch(path, chan))),
            Err(e) => {
                eprintln!("ServiceFs failed to convert channel to fasync channel: {:?}", e);
                None
            }
        }
    }
}

/// A `!Send` (non-thread-safe) trait object encapsulating a `Service` with
/// the given `Output` type.
///
/// Types which implement the `Service` trait can be converted to objects of
/// this type via the `From`/`Into` traits.
pub struct ServiceObjLocal<'a, Output>(Box<dyn Service<Output = Output> + 'a>);

impl<'a, S: Service + 'a> From<S> for ServiceObjLocal<'a, S::Output> {
    fn from(service: S) -> Self {
        ServiceObjLocal(Box::new(service))
    }
}

/// A thread-safe (`Send`) trait object encapsulating a `Service` with
/// the given `Output` type.
///
/// Types which implement the `Service` trait and the `Send` trait can
/// be converted to objects of this type via the `From`/`Into` traits.
pub struct ServiceObj<'a, Output>(Box<dyn Service<Output = Output> + Send + 'a>);

impl<'a, S: Service + Send + 'a> From<S> for ServiceObj<'a, S::Output> {
    fn from(service: S) -> Self {
        ServiceObj(Box::new(service))
    }
}

/// A trait implemented by both `ServiceObj` and `ServiceObjLocal` that
/// allows code to be generic over thread-safety.
///
/// Code that uses `ServiceObj` will require `Send` bounds but will be
/// multithreaded-capable, while code that uses `ServiceObjLocal` will
/// allow non-`Send` types but will be restricted to singlethreaded
/// executors.
pub trait ServiceObjTrait {
    /// The output type of the underlying `Service`.
    type Output;

    /// Get a mutable reference to the underlying `Service` trait object.
    fn service(&mut self) -> &mut dyn Service<Output = Self::Output>;
}

impl<'a, Output> ServiceObjTrait for ServiceObjLocal<'a, Output> {
    type Output = Output;

    fn service(&mut self) -> &mut dyn Service<Output = Self::Output> {
        &mut *self.0
    }
}

impl<'a, Output> ServiceObjTrait for ServiceObj<'a, Output> {
    type Output = Output;

    fn service(&mut self) -> &mut dyn Service<Output = Self::Output> {
        &mut *self.0
    }
}
