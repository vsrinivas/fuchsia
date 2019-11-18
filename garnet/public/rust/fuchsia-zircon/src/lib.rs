// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon kernel
//! [syscalls](https://fuchsia.dev/fuchsia-src/reference/syscalls).

pub mod sys {
    pub use fuchsia_zircon_sys::*;
}

// Implements the HandleBased traits for a Handle newtype struct
macro_rules! impl_handle_based {
    ($type_name:path) => {
        impl AsHandleRef for $type_name {
            fn as_handle_ref(&self) -> HandleRef {
                self.0.as_handle_ref()
            }
        }

        impl From<Handle> for $type_name {
            fn from(handle: Handle) -> Self {
                $type_name(handle)
            }
        }

        impl From<$type_name> for Handle {
            fn from(x: $type_name) -> Handle {
                x.0
            }
        }

        impl HandleBased for $type_name {}
    };
}

/// Convenience macro for creating get/set property functions on an object.
///
/// This is for use when the underlying property type is a simple raw type.
/// It creates an empty 'tag' struct to implement the relevant PropertyQuery*
/// traits against. One, or both, of a getting and setter may be defined
/// depending upon what the property supports. Example usage is
/// unsafe_handle_propertyes!(ObjectType[get_foo_prop,set_foo_prop:FooPropTag,FOO,u32;]);
/// unsafe_handle_properties!(object: Foo,
///     props: [
///         {query_ty: FOO_BAR, tag: FooBarTag, prop_ty: usize, get:get_bar},
///         {query_ty: FOO_BAX, tag: FooBazTag, prop_ty: u32, set:set_baz},
///     ]
/// );
/// And will create
/// Foo::get_bar(&self) -> Result<usize, Status>
/// Foo::set_baz(&self, val: &u32) -> Result<(), Status>
/// Using Property::FOO as the underlying property.
///
///  # Safety
///
/// This macro will implement unsafe traits on your behalf and any combination
/// of query_ty and prop_ty must respect the Safety requirements detailed on the
/// PropertyQuery trait.
macro_rules! unsafe_handle_properties {
    (
        object: $object_ty:ty,
        props: [$( {
            query_ty: $query_ty:ident,
            tag: $query_tag:ident,
            prop_ty: $prop_ty:ty
            $(,get: $get:ident)*
            $(,set: $set:ident)*
            $(,)*
        }),*$(,)*]
    ) => {
        $(
            struct $query_tag {}
            unsafe impl PropertyQuery for $query_tag {
                const PROPERTY: Property = Property::$query_ty;
                type PropTy = $prop_ty;
            }

            $(
                unsafe impl PropertyQueryGet for $query_tag {}
                impl $object_ty {
                    pub fn $get(&self) -> Result<$prop_ty, Status> {
                        object_get_property::<$query_tag>(self.as_handle_ref())
                    }
                }
            )*

            $(
                unsafe impl PropertyQuerySet for $query_tag {}
                impl $object_ty {
                    pub fn $set(&self, val: &$prop_ty) -> Result<(), Status> {
                        object_set_property::<$query_tag>(self.as_handle_ref(), val)
                    }
                }
            )*
        )*
    }
}

// Creates associated constants of TypeName of the form
// `pub const NAME: TypeName = TypeName(path::to::value);`
// and provides a private `assoc_const_name` method and a `Debug` implementation
// for the type based on `$name`.
// If multiple names match, the first will be used in `name` and `Debug`.
#[macro_export]
macro_rules! assoc_values {
    ($typename:ident, [$($(#[$attr:meta])* $name:ident = $value:path;)*]) => {
        #[allow(non_upper_case_globals)]
        impl $typename {
            $(
                $(#[$attr])*
                pub const $name: $typename = $typename($value);
            )*

            fn assoc_const_name(&self) -> Option<&'static str> {
                match self.0 {
                    $(
                        $(#[$attr])*
                        $value => Some(stringify!($name)),
                    )*
                    _ => None,
                }
            }
        }

        impl ::std::fmt::Debug for $typename {
            fn fmt(&self, f: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
                f.write_str(concat!(stringify!($typename), "("))?;
                match self.assoc_const_name() {
                    Some(name) => f.write_str(&name)?,
                    None => ::std::fmt::Debug::fmt(&self.0, f)?,
                }
                f.write_str(")")
            }
        }
    }
}

mod channel;
mod cprng;
mod debuglog;
mod event;
mod eventpair;
mod fifo;
pub mod guest;
mod handle;
mod info;
mod interrupt;
mod job;
mod port;
mod process;
mod property;
mod resource;
mod rights;
mod signals;
mod socket;
mod task;
mod thread;
mod time;
mod vmar;
mod vmo;

pub use self::channel::*;
pub use self::cprng::*;
pub use self::debuglog::*;
pub use self::event::*;
pub use self::eventpair::*;
pub use self::fifo::*;
pub use self::guest::{GPAddr, Guest};
pub use self::handle::*;
pub use self::info::*;
pub use self::interrupt::*;
pub use self::job::*;
pub use self::port::*;
pub use self::process::*;
pub use self::property::*;
pub use self::resource::*;
pub use self::rights::*;
pub use self::signals::*;
pub use self::socket::*;
pub use self::task::*;
pub use self::thread::*;
pub use self::time::*;
pub use self::vmar::*;
pub use self::vmo::*;
pub use fuchsia_zircon_status::*;

/// Prelude containing common utility traits.
/// Designed for use like `use fuchsia_zircon::prelude::*;`
pub mod prelude {
    pub use crate::{AsHandleRef, DurationNum, HandleBased, Peered};
}

/// Convenience re-export of `Status::ok`.
pub fn ok(raw: sys::zx_status_t) -> Result<(), Status> {
    Status::ok(raw)
}

/// A "wait item" containing a handle reference and information about what signals
/// to wait on, and, on return from `object_wait_many`, which are pending.
#[repr(C)]
#[derive(Debug)]
pub struct WaitItem<'a> {
    /// The handle to wait on.
    pub handle: HandleRef<'a>,
    /// A set of signals to wait for.
    pub waitfor: Signals,
    /// The set of signals pending, on return of `object_wait_many`.
    pub pending: Signals,
}

/// An identifier to select a particular clock. See
/// [zx_clock_get](https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_get.md)
/// for more information about the possible values.
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum ClockId {
    /// The number of nanoseconds since the system was powered on. Corresponds to
    /// `ZX_CLOCK_MONOTONIC`.
    Monotonic = 0,
    /// The number of wall clock nanoseconds since the Unix epoch (midnight on January 1 1970) in
    /// UTC. Corresponds to ZX_CLOCK_UTC.
    UTC = 1,
    /// The number of nanoseconds the current thread has been running for. Corresponds to
    /// ZX_CLOCK_THREAD.
    Thread = 2,
}

/// Wait on multiple handles.
/// The success return value is a bool indicating whether one or more of the
/// provided handle references was closed during the wait.
///
/// Wraps the
/// [zx_object_wait_many](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_wait_many.md)
/// syscall.
pub fn object_wait_many(items: &mut [WaitItem], deadline: Time) -> Result<bool, Status> {
    let items_ptr = items.as_mut_ptr() as *mut sys::zx_wait_item_t;
    let status = unsafe { sys::zx_object_wait_many(items_ptr, items.len(), deadline.into_nanos()) };
    if status == sys::ZX_ERR_CANCELED {
        return Ok(true);
    }
    ok(status).map(|()| false)
}

/// Query information about a zircon object.
/// Returns `(num_returned, num_remaining)` on success.
pub fn object_get_info<Q: ObjectQuery>(
    handle: HandleRef,
    out: &mut [Q::InfoTy],
) -> Result<(usize, usize), Status> {
    let mut actual = 0;
    let mut avail = 0;
    let status = unsafe {
        sys::zx_object_get_info(
            handle.raw_handle(),
            *Q::TOPIC,
            out.as_mut_ptr() as *mut u8,
            std::mem::size_of_val(out),
            &mut actual as *mut usize,
            &mut avail as *mut usize,
        )
    };
    ok(status).map(|_| (actual, avail - actual))
}

/// Get a property on a zircon object
pub fn object_get_property<P: PropertyQueryGet>(handle: HandleRef) -> Result<P::PropTy, Status> {
    // this is safe due to the contract on the P::PropTy type in the ObjectProperty trait.
    let mut out = ::std::mem::MaybeUninit::<P::PropTy>::uninit();
    let status = unsafe {
        sys::zx_object_get_property(
            handle.raw_handle(),
            *P::PROPERTY,
            out.as_mut_ptr() as *mut u8,
            std::mem::size_of::<P::PropTy>(),
        )
    };
    ok(status).map(|_| unsafe { out.assume_init() })
}

/// Set a property on a zircon object
pub fn object_set_property<P: PropertyQuerySet>(
    handle: HandleRef,
    val: &P::PropTy,
) -> Result<(), Status> {
    let status = unsafe {
        sys::zx_object_set_property(
            handle.raw_handle(),
            *P::PROPERTY,
            val as *const P::PropTy as *const u8,
            std::mem::size_of::<P::PropTy>(),
        )
    };
    ok(status)
}

#[cfg(test)]
mod tests {
    #[allow(unused_imports)]
    use super::prelude::*;
    use super::*;

    #[test]
    fn monotonic_time_increases() {
        let time1 = Time::get(ClockId::Monotonic);
        1_000.nanos().sleep();
        let time2 = Time::get(ClockId::Monotonic);
        assert!(time2 > time1);
    }

    #[test]
    fn utc_time_increases() {
        let time1 = Time::get(ClockId::UTC);
        1_000.nanos().sleep();
        let time2 = Time::get(ClockId::UTC);
        assert!(time2 > time1);
    }

    #[test]
    fn thread_time_increases() {
        let time1 = Time::get(ClockId::Thread);
        1_000.nanos().sleep();
        let time2 = Time::get(ClockId::Thread);
        assert!(time2 > time1);
    }

    #[test]
    fn ticks_increases() {
        let ticks1 = ticks_get();
        1_000.nanos().sleep();
        let ticks2 = ticks_get();
        assert!(ticks2 > ticks1);
    }

    #[test]
    fn tick_length() {
        let sleep_time = 1.milli();
        let ticks1 = ticks_get();
        sleep_time.sleep();
        let ticks2 = ticks_get();

        // The number of ticks should have increased by at least 1 ms worth
        let sleep_ticks = (sleep_time.into_millis() as i64) * ticks_per_second() / 1000;
        assert!(ticks2 >= (ticks1 + sleep_ticks));
    }

    #[test]
    fn into_raw() {
        let vmo = Vmo::create(1).unwrap();
        let h = vmo.into_raw();
        let vmo2 = Vmo::from(unsafe { Handle::from_raw(h) });
        assert!(vmo2.write(b"1", 0).is_ok());
    }

    #[test]
    fn sleep() {
        let sleep_ns = 1.millis();
        let time1 = Time::get(ClockId::Monotonic);
        sleep_ns.sleep();
        let time2 = Time::get(ClockId::Monotonic);
        assert!(time2 > time1 + sleep_ns);
    }

    /// Test duplication by means of a VMO
    #[test]
    fn duplicate() {
        let hello_length: usize = 5;

        // Create a VMO and write some data to it.
        let vmo = Vmo::create(hello_length as u64).unwrap();
        assert!(vmo.write(b"hello", 0).is_ok());

        // Replace, reducing rights to read.
        let readonly_vmo = vmo.duplicate_handle(Rights::READ).unwrap();
        // Make sure we can read but not write.
        let mut read_vec = vec![0; hello_length];
        assert!(readonly_vmo.read(&mut read_vec, 0).is_ok());
        assert_eq!(read_vec, b"hello");
        assert_eq!(readonly_vmo.write(b"", 0), Err(Status::ACCESS_DENIED));

        // Write new data to the original handle, and read it from the new handle
        assert!(vmo.write(b"bye", 0).is_ok());
        assert!(readonly_vmo.read(&mut read_vec, 0).is_ok());
        assert_eq!(read_vec, b"byelo");
    }

    // Test replace by means of a VMO
    #[test]
    fn replace() {
        let hello_length: usize = 5;

        // Create a VMO and write some data to it.
        let vmo = Vmo::create(hello_length as u64).unwrap();
        assert!(vmo.write(b"hello", 0).is_ok());

        // Replace, reducing rights to read.
        let readonly_vmo = vmo.replace_handle(Rights::READ).unwrap();
        // Make sure we can read but not write.
        let mut read_vec = vec![0; hello_length];
        assert!(readonly_vmo.read(&mut read_vec, 0).is_ok());
        assert_eq!(read_vec, b"hello");
        assert_eq!(readonly_vmo.write(b"", 0), Err(Status::ACCESS_DENIED));
    }

    #[test]
    fn wait_and_signal() {
        let event = Event::create().unwrap();
        let ten_ms = 10.millis();

        // Waiting on it without setting any signal should time out.
        assert_eq!(event.wait_handle(Signals::USER_0, Time::after(ten_ms)), Err(Status::TIMED_OUT));

        // If we set a signal, we should be able to wait for it.
        assert!(event.signal_handle(Signals::NONE, Signals::USER_0).is_ok());
        assert_eq!(
            event.wait_handle(Signals::USER_0, Time::after(ten_ms)).unwrap(),
            Signals::USER_0
        );

        // Should still work, signals aren't automatically cleared.
        assert_eq!(
            event.wait_handle(Signals::USER_0, Time::after(ten_ms)).unwrap(),
            Signals::USER_0
        );

        // Now clear it, and waiting should time out again.
        assert!(event.signal_handle(Signals::USER_0, Signals::NONE).is_ok());
        assert_eq!(event.wait_handle(Signals::USER_0, Time::after(ten_ms)), Err(Status::TIMED_OUT));
    }

    #[test]
    fn wait_many_and_signal() {
        let ten_ms = 10.millis();
        let e1 = Event::create().unwrap();
        let e2 = Event::create().unwrap();

        // Waiting on them now should time out.
        let mut items = vec![
            WaitItem {
                handle: e1.as_handle_ref(),
                waitfor: Signals::USER_0,
                pending: Signals::NONE,
            },
            WaitItem {
                handle: e2.as_handle_ref(),
                waitfor: Signals::USER_1,
                pending: Signals::NONE,
            },
        ];
        assert_eq!(object_wait_many(&mut items, Time::after(ten_ms)), Err(Status::TIMED_OUT));
        assert_eq!(items[0].pending, Signals::NONE);
        assert_eq!(items[1].pending, Signals::NONE);

        // Signal one object and it should return success.
        assert!(e1.signal_handle(Signals::NONE, Signals::USER_0).is_ok());
        assert!(object_wait_many(&mut items, Time::after(ten_ms)).is_ok());
        assert_eq!(items[0].pending, Signals::USER_0);
        assert_eq!(items[1].pending, Signals::NONE);

        // Signal the other and it should return both.
        assert!(e2.signal_handle(Signals::NONE, Signals::USER_1).is_ok());
        assert!(object_wait_many(&mut items, Time::after(ten_ms)).is_ok());
        assert_eq!(items[0].pending, Signals::USER_0);
        assert_eq!(items[1].pending, Signals::USER_1);

        // Clear signals on both; now it should time out again.
        assert!(e1.signal_handle(Signals::USER_0, Signals::NONE).is_ok());
        assert!(e2.signal_handle(Signals::USER_1, Signals::NONE).is_ok());
        assert_eq!(object_wait_many(&mut items, Time::after(ten_ms)), Err(Status::TIMED_OUT));
        assert_eq!(items[0].pending, Signals::NONE);
        assert_eq!(items[1].pending, Signals::NONE);
    }
}

pub fn usize_into_u32(n: usize) -> Result<u32, ()> {
    if n > ::std::u32::MAX as usize || n < ::std::u32::MIN as usize {
        return Err(());
    }
    Ok(n as u32)
}

pub fn size_to_u32_sat(n: usize) -> u32 {
    if n > ::std::u32::MAX as usize {
        return ::std::u32::MAX;
    }
    if n < ::std::u32::MIN as usize {
        return ::std::u32::MIN;
    }
    n as u32
}
