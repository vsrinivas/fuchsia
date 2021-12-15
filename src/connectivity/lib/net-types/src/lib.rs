// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Networking types and operations.
//!
//! This crate defines types and operations useful for operating with various
//! network protocols. Some general utilities are defined in the crate root,
//! while protocol-specific operations are defined in their own modules.
//!
//! # Witness types
//!
//! This crate makes heavy use of the "witness type" pattern. A witness type is
//! one whose existence "bears witness" to a particular property. For example,
//! the [`UnicastAddr`] type wraps an existing address and guarantees that it
//! is unicast.
//!
//! There are a few components to a witness type.
//!
//! First, each property is encoded in a trait. For example, the
//! [`UnicastAddress`] trait is implemented by any address type which can be
//! unicast. The [`is_unicast`] method is used to determine whether a given
//! instance is unicast.
//!
//! Second, a witness type wraps an address. For example, `UnicastAddr<A>` can
//! be used with any `A: UnicastAddress`. There are two ways to obtain an
//! instance of a witness type. Some constants are constructed as witness types
//! at compile time, and so provide a static guarantee of the witnessed property
//! (e.g., [`Ipv6::LOOPBACK_IPV6_ADDRESS`] is a `UnicastAddr`). Otherwise, an
//! instance can be constructed fallibly at runtime. For example,
//! [`UnicastAddr::new`] accepts an `A` and returns an `Option<UnicastAddr<A>>`,
//! returning `Some` if the address passes the `is_unicast` check, and `None`
//! otherwise.
//!
//! Finally, each witness type implements the [`Witness`] trait, which allows
//! code to be written which is generic over which witness type is used.
//!
//! Witness types enable a variety of operations which are only valid on certain
//! types of addresses. For example, a multicast MAC address can be derived from
//! a multicast IPv6 address, so the `MulticastAddr<Mac>` type implements
//! `From<MulticastAddr<Ipv6Addr>>`. Similarly, given an [`Ipv6Addr`], the
//! [`to_solicited_node_address`] method can be used to construct the address's
//! solicited-node address, which is a `MulticastAddr<Ipv6Addr>`. Combining
//! these, it's possible to take an `Ipv6Addr` and compute the solicited node
//! address's multicast MAC address without performing any runtime validation:
//!
//! ```rust
//! # use net_types::ethernet::Mac;
//! # use net_types::ip::Ipv6Addr;
//! # use net_types::MulticastAddr;
//! fn to_solicited_node_multicast_mac(addr: &Ipv6Addr) -> MulticastAddr<Mac> {
//!     addr.to_solicited_node_address().into()
//! }
//! ```
//!
//! # Naming Conventions
//!
//! When both types and traits exist which represent the same concept, the
//! traits will be given a full name - such as [`IpAddress`] or
//! [`UnicastAddress`] - while the types will be given an abbreviated name -
//! such as [`IpAddr`], [`Ipv4Addr`], [`Ipv6Addr`], or [`UnicastAddr`].
//!
//! [`is_unicast`]: crate::UnicastAddress::is_unicast
//! [`Ipv6::LOOPBACK_IPV6_ADDRESS`]: crate::ip::Ipv6::LOOPBACK_IPV6_ADDRESS
//! [`to_solicited_node_address`]: crate::ip::Ipv6Addr::to_solicited_node_address
//! [`IpAddress`]: crate::ip::IpAddress
//! [`IpAddr`]: crate::ip::IpAddr
//! [`Ipv4Addr`]: crate::ip::Ipv4Addr
//! [`Ipv6Addr`]: crate::ip::Ipv6Addr

#![deny(missing_docs)]
#![cfg_attr(all(not(feature = "std"), not(test)), no_std)]

pub mod ethernet;
pub mod ip;

use core::convert::TryFrom;
use core::fmt::{self, Display, Formatter};
use core::ops::Deref;

mod sealed {
    // Used to ensure that certain traits cannot be implemented by anyone
    // outside this crate, such as the Ip and IpAddress traits.
    pub trait Sealed {}
}

/// A type which is a witness to some property about an address.
///
/// A type which implements `Witness<A>` wraps an address of type `A` and
/// guarantees some property about the wrapped address. It is implemented by
/// [`SpecifiedAddr`], [`UnicastAddr`], [`MulticastAddr`], and
/// [`LinkLocalAddr`].
pub trait Witness<A>: AsRef<A> + Sized + sealed::Sealed {
    /// Constructs a new witness type.
    ///
    /// `new` returns `None` if `addr` does not satisfy the property guaranteed
    /// by `Self`.
    fn new(addr: A) -> Option<Self>;

    /// Constructs a new witness type without checking to see if `addr` actually
    /// satisfies the required property.
    ///
    /// # Safety
    ///
    /// It is up to the caller to make sure that `addr` satisfies the required
    /// property in order to avoid breaking the guarantees of this trait.
    unsafe fn new_unchecked(addr: A) -> Self;

    /// Constructs a new witness type from an existing witness type.
    ///
    /// `from_witness(witness)` is equivalent to `new(witness.into_addr())`.
    fn from_witness<W: Witness<A>>(addr: W) -> Option<Self> {
        Self::new(addr.into_addr())
    }

    // In a previous version of this code, we did `fn get(self) -> A where Self:
    // Copy` (taking `self` by value and using `where Self: Copy`). That felt
    // marginally cleaner, but it turns out that there are cases in which the
    // user only has access to a reference and still wants to be able to call
    // `get` without having to do the ugly `(*addr).get()`.

    /// Gets a copy of the address.
    #[inline]
    fn get(&self) -> A
    where
        A: Copy,
    {
        *self.as_ref()
    }

    /// Consumes this witness and returns the contained `A`.
    ///
    /// If `A: Copy`, prefer [`get`] instead of `into_addr`. `get` is idiomatic
    /// for wrapper types which which wrap `Copy` types (e.g., see
    /// [`NonZeroUsize::get`] or [`Cell::get`]). `into_xxx` methods are
    /// idiomatic only when `self` must be consumed by value because the wrapped
    /// value is not `Copy` (e.g., see [`Cell::into_inner`]).
    ///
    /// [`get`]: Witness::get
    /// [`NonZeroUsize::get`]: core::num::NonZeroUsize::get
    /// [`Cell::get`]: core::cell::Cell::get
    /// [`Cell::into_inner`]: core::cell::Cell::into_inner
    fn into_addr(self) -> A;
}

// NOTE: The "witness" types UnicastAddr, MulticastAddr, and LinkLocalAddr -
// which provide the invariant that the value they contain is a unicast,
// multicast, or link-local address, respectively - cannot actually guarantee
// this property without certain promises from the implementations of the
// UnicastAddress, MulticastAddress, and LinkLocalAddress traits that they rely
// on. In particular, the values must be "immutable" in the sense that, given
// only immutable references to the values, nothing about the values can change
// such that the "unicast-ness", "multicast-ness" or "link-local-ness" of the
// values change. Since the UnicastAddress, MulticastAddress, and
// LinkLocalAddress traits are not unsafe traits, it would be unsound for unsafe
// code to rely for its soundness on this behavior. For a more in-depth
// discussion of why this isn't possible without an explicit opt-in on the part
// of the trait implementor, see this forum thread:
// https://users.rust-lang.org/t/prevent-interior-mutability/29403

/// Implements a trait for a witness type.
///
/// `impl_trait_for_witness` implements `$trait` for `$witness<A>` if `A:
/// $trait`.
macro_rules! impl_trait_for_witness {
    ($trait:ident, $method:ident, $witness:ident) => {
        impl<A: $trait> $trait for $witness<A> {
            fn $method(&self) -> bool {
                self.0.$method()
            }
        }
    };
}

/// Addresses that can be specified.
///
/// `SpecifiedAddress` is implemented by address types for which some values are
/// considered [unspecified] addresses. Unspecified addresses are usually not
/// legal to be used in actual network traffic, and are only meant to represent
/// the lack of any defined address. The exact meaning of the unspecified
/// address often varies by context. For example, the IPv4 address 0.0.0.0 and
/// the IPv6 address :: can be used, in the context of creating a listening
/// socket on systems that use the BSD sockets API, to listen on all local IP
/// addresses rather than a particular one.
///
/// [unspecified]: https://en.wikipedia.org/wiki/0.0.0.0
pub trait SpecifiedAddress {
    /// Is this a specified address?
    ///
    /// `is_specified` must maintain the invariant that, if it is called twice
    /// on the same object, and in between those two calls, no code has operated
    /// on a mutable reference to that object, both calls will return the same
    /// value. This property is required in order to implement
    /// [`SpecifiedAddr`]. Note that, since this is not an `unsafe` trait,
    /// `unsafe` code may NOT rely on this property for its soundness. However,
    /// code MAY rely on this property for its correctness.
    fn is_specified(&self) -> bool;
}

impl_trait_for_witness!(SpecifiedAddress, is_specified, UnicastAddr);
impl_trait_for_witness!(SpecifiedAddress, is_specified, MulticastAddr);
impl_trait_for_witness!(SpecifiedAddress, is_specified, BroadcastAddr);
impl_trait_for_witness!(SpecifiedAddress, is_specified, LinkLocalAddr);

/// Addresses that can be unicast.
///
/// `UnicastAddress` is implemented by address types for which some values are
/// considered [unicast] addresses. Unicast addresses are used to identify a
/// single network node, as opposed to broadcast and multicast addresses, which
/// identify a group of nodes.
///
/// `UnicastAddress` is only implemented for addresses whose unicast-ness can be
/// determined by looking only at the address itself (this is notably not true
/// for IPv4 addresses, which can be considered broadcast addresses depending on
/// the subnet in which they are used).
///
/// [unicast]: https://en.wikipedia.org/wiki/Unicast
pub trait UnicastAddress {
    /// Is this a unicast address?
    ///
    /// `is_unicast` must maintain the invariant that, if it is called twice on
    /// the same object, and in between those two calls, no code has operated on
    /// a mutable reference to that object, both calls will return the same
    /// value. This property is required in order to implement [`UnicastAddr`].
    /// Note that, since this is not an `unsafe` trait, `unsafe` code may NOT
    /// rely on this property for its soundness. However, code MAY rely on this
    /// property for its correctness.
    ///
    /// If this type also implements [`SpecifiedAddress`], then `a.is_unicast()`
    /// implies `a.is_specified()`.
    fn is_unicast(&self) -> bool;
}

impl_trait_for_witness!(UnicastAddress, is_unicast, SpecifiedAddr);
impl_trait_for_witness!(UnicastAddress, is_unicast, MulticastAddr);
impl_trait_for_witness!(UnicastAddress, is_unicast, BroadcastAddr);
impl_trait_for_witness!(UnicastAddress, is_unicast, LinkLocalAddr);

/// Addresses that can be multicast.
///
/// `MulticastAddress` is implemented by address types for which some values are
/// considered [multicast] addresses. Multicast addresses are used to identify a
/// group of multiple network nodes, as opposed to unicast addresses, which
/// identify a single node, or broadcast addresses, which identify all the nodes
/// in some region of a network.
///
/// [multicast]: https://en.wikipedia.org/wiki/Multicast
pub trait MulticastAddress {
    /// Is this a unicast address?
    ///
    /// `is_multicast` must maintain the invariant that, if it is called twice
    /// on the same object, and in between those two calls, no code has operated
    /// on a mutable reference to that object, both calls will return the same
    /// value. This property is required in order to implement
    /// [`MulticastAddr`]. Note that, since this is not an `unsafe` trait,
    /// `unsafe` code may NOT rely on this property for its soundness. However,
    /// code MAY rely on this property for its correctness.
    ///
    /// If this type also implements [`SpecifiedAddress`], then
    /// `a.is_multicast()` implies `a.is_specified()`.
    fn is_multicast(&self) -> bool;
}

impl_trait_for_witness!(MulticastAddress, is_multicast, SpecifiedAddr);
impl_trait_for_witness!(MulticastAddress, is_multicast, UnicastAddr);
impl_trait_for_witness!(MulticastAddress, is_multicast, BroadcastAddr);
impl_trait_for_witness!(MulticastAddress, is_multicast, LinkLocalAddr);

/// Addresses that can be broadcast.
///
/// `BroadcastAddress` is implemented by address types for which some values are
/// considered [broadcast] addresses. Broadcast addresses are used to identify
/// all the nodes in some region of a network, as opposed to unicast addresses,
/// which identify a single node, or multicast addresses, which identify a group
/// of nodes (not necessarily all of them).
///
/// [broadcast]: https://en.wikipedia.org/wiki/Broadcasting_(networking)
pub trait BroadcastAddress {
    /// Is this a broadcast address?
    ///
    /// If this type also implements [`SpecifiedAddress`], then
    /// `a.is_broadcast()` implies `a.is_specified()`.
    fn is_broadcast(&self) -> bool;
}

impl_trait_for_witness!(BroadcastAddress, is_broadcast, SpecifiedAddr);
impl_trait_for_witness!(BroadcastAddress, is_broadcast, UnicastAddr);
impl_trait_for_witness!(BroadcastAddress, is_broadcast, MulticastAddr);
impl_trait_for_witness!(BroadcastAddress, is_broadcast, LinkLocalAddr);

/// Addresses that can be a link-local.
///
/// `LinkLocalAddress` is implemented by address types for which some values are
/// considered [link-local] addresses. Link-local addresses are used for
/// communication within a network segment, as opposed to global/public
/// addresses which may be used for communication across networks.
///
/// `LinkLocalAddress` is only implemented for addresses whose link-local-ness
/// can be determined by looking only at the address itself.
///
/// [link-local]: https://en.wikipedia.org/wiki/Link-local_address
pub trait LinkLocalAddress {
    /// Is this a link-local address?
    ///
    /// `is_link_local` must maintain the invariant that, if it is called twice
    /// on the same object, and in between those two calls, no code has operated
    /// on a mutable reference to that object, both calls will return the same
    /// value. This property is required in order to implement
    /// [`LinkLocalAddr`]. Note that, since this is not an `unsafe` trait,
    /// `unsafe` code may NOT rely on this property for its soundness. However,
    /// code MAY rely on this property for its correctness.
    ///
    /// If this type also implements [`SpecifiedAddress`], then
    /// `a.is_link_local()` implies `a.is_specified()`.
    fn is_link_local(&self) -> bool;
}

impl_trait_for_witness!(LinkLocalAddress, is_link_local, SpecifiedAddr);
impl_trait_for_witness!(LinkLocalAddress, is_link_local, UnicastAddr);
impl_trait_for_witness!(LinkLocalAddress, is_link_local, MulticastAddr);
impl_trait_for_witness!(LinkLocalAddress, is_link_local, BroadcastAddr);

/// A scope used by [`ScopeableAddress`]. See that trait's documentation for
/// more information.
///
/// `Scope` is implemented for `()`. No addresses with the `()` scope can ever
/// have an associated zone (in other words, `().can_have_zone()` always returns
/// `false`).
pub trait Scope {
    /// Can addresses in this scope have an associated zone?
    fn can_have_zone(&self) -> bool;
}

impl Scope for () {
    fn can_have_zone(&self) -> bool {
        false
    }
}

/// An address that can be tied to some scope identifier.
///
/// `ScopeableAddress` is implemented by address types for which some values can
/// have extra scoping information attached. Notably, some IPv6 addresses
/// belonging to a particular scope class require extra metadata to identify the
/// scope identifier or "zone". The zone is typically the networking interface
/// identifier.
///
/// Address types which are never in any identified scope may still implement
/// `ScopeableAddress` by setting the associated `Scope` type to `()`, which has
/// the effect of ensuring that a zone can never be associated with an address
/// (since the implementation of [`Scope::can_have_zone`] for `()` always
/// returns `false`).
pub trait ScopeableAddress {
    /// The type of all non-global scopes.
    type Scope: Scope;

    /// The scope of this address.
    ///
    /// `scope` must maintain the invariant that, if it is called twice on the
    /// same object, and in between those two calls, no code has operated on a
    /// mutable reference to that object, both calls will return the same value.
    /// This property is required in order to implement [`AddrAndZone`]. Note
    /// that, since this is not an `unsafe` trait, `unsafe` code may NOT rely on
    /// this property for its soundness. However, code MAY rely on this property
    /// for its correctness.
    ///
    /// If this type also implements [`SpecifiedAddress`] then
    /// `a.scope().can_have_zone()` implies `a.is_specified()`, since
    /// unspecified addresses are always global, and the global scope cannot
    /// have a zone.
    fn scope(&self) -> Self::Scope;
}

macro_rules! doc_comment {
    ($x:expr, $($tt:tt)*) => {
        #[doc = $x]
        $($tt)*
    };
}

/// Define a witness type and implement methods and traits for it.
///
/// - `$type` is the type's name
/// - `$adj` is a string literal representing the adjective used to describe
///   addresses of this type for documentation purposes (e.g., "specified",
///   "unicast", etc)
/// - `$trait` is the name of the trait associated with the property to be
///   witnessed
/// - `$method` is the method on `$trait` which determines whether the property
///   holds (e.g., `is_specified`)
macro_rules! impl_witness {
    ($type:ident, $adj:literal, $trait:ident, $method:ident) => {
        doc_comment! {
        concat!("An address which is guaranteed to be ", $adj, ".

`", stringify!($type), "` wraps an address of type `A` and guarantees that it is
a ", $adj, " address. Note that this guarantee is contingent on a correct
implementation of the [`", stringify!($trait), "`] trait. Since that trait is
not `unsafe`, `unsafe` code may NOT rely on this guarantee for its soundness."),
            #[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
            pub struct $type<A>(A);
        }

        impl<A: $trait> $type<A> {
            // NOTE(joshlf): It may seem odd to include `new` and `from_witness`
            // constructors here when they already exists on the `Witness`
            // trait, which this type implements. The reason we do this is that,
            // since many of these types implement the `Witness` trait multiple
            // times (e.g., `Witness<A> for LinkLocalAddr<A>` and `Witness<A>
            // for LinkLocalAddr<MulticastAddr<A>`), if we didn't provide these
            // constructors, callers invoking `Foo::new` or `Foo::from_witness`
            // would need to `use` the `Witness` trait, and the compiler often
            // doesn't have enough information to figure out which `Witness`
            // implementation is meant in a given situation. This, in turn,
            // requires a lot of boilerplate type annotations on the part of
            // users. Providing these constructors helps alleviate this problem.

            doc_comment! {
                concat!("Constructs a new `", stringify!($type), "`.

`new` returns `None` if `!addr.", stringify!($method), "()`."),
                #[inline]
                pub fn new(addr: A) -> Option<$type<A>> {
                    if !addr.$method() {
                        return None;
                    }
                    Some($type(addr))
                }
            }

            doc_comment! {
                concat!("Constructs a new `", stringify!($type), "` from a
witness type.

`from_witness(witness)` is equivalent to `new(witness.into_addr())`."),
                pub fn from_witness<W: Witness<A>>(addr: W) -> Option<$type<A>> {
                    $type::new(addr.into_addr())
                }
            }
        }

        // TODO(https://github.com/rust-lang/rust/issues/57563): Once traits
        // other than `Sized` are supported for const fns, move this into the
        // block with the `A: $trait` bound.
        impl<A> $type<A> {
            doc_comment! {
                concat!("Constructs a new `", stringify!($type), "` without
checking to see if `addr` is actually ", $adj, ".

# Safety

It is up to the caller to make sure that `addr` is ", $adj, " to avoid breaking
the guarantees of `", stringify!($type), "`. See [`", stringify!($type), "`] for
more details."),
                pub const unsafe fn new_unchecked(addr: A) -> $type<A> {
                    $type(addr)
                }
            }
        }

        impl<A> sealed::Sealed for $type<A> {}
        impl<A: $trait> Witness<A> for $type<A> {
            fn new(addr: A) -> Option<$type<A>> {
                $type::new(addr)
            }

            unsafe fn new_unchecked(addr: A) -> $type<A> {
                $type(addr)
            }

            #[inline]
            fn into_addr(self) -> A {
                self.0
            }
        }

        impl<A: $trait> AsRef<A> for $type<A> {
            fn as_ref(&self) -> &A {
                &self.0
            }
        }

        impl<A: $trait> Deref for $type<A> {
            type Target = A;

            #[inline]
            fn deref(&self) -> &A {
                &self.0
            }
        }

        impl<A: $trait + Display> Display for $type<A> {
            #[inline]
            fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
                self.0.fmt(f)
            }
        }
    };
}

/// Implements an `into_specified` method on the witness type `$type`.
///
/// - `$trait` is the name of the trait associated with the witnessed property
/// - `$method` is the method on `$trait` which determines whether the property
///   holds (e.g., `is_unicast`)
///
/// An `into_specified` method is predicated on the witnessed property implying
/// that the address is also specified (e.g., `UnicastAddress::is_unicast`
/// implies `SpecifiedAddress::is_specified`).
macro_rules! impl_into_specified {
    ($type:ident, $trait:ident, $method:ident) => {
        impl<A: $trait + SpecifiedAddress> $type<A> {
            doc_comment! {
                concat!("Converts this `", stringify!($type), "` into a
[`SpecifiedAddr`].

[`", stringify!($trait), "::", stringify!($method), "`] implies
[`SpecifiedAddress::is_specified`], so all `", stringify!($type), "`s are
guaranteed to be specified, so this conversion is infallible."),
                #[inline]
                pub fn into_specified(self) -> SpecifiedAddr<A> {
                    SpecifiedAddr(self.0)
                }
            }
        }

        impl<A: $trait + SpecifiedAddress> From<$type<A>> for SpecifiedAddr<A> {
            fn from(addr: $type<A>) -> SpecifiedAddr<A> {
                addr.into_specified()
            }
        }
    };
}

/// Implements [`Witness`] for a nested witness type.
///
/// `impl_nested_witness` implements `Witness<A>` for
/// `$outer_type<$inner_type<A>>`.
macro_rules! impl_nested_witness {
    ($outer_trait:ident, $outer_type:ident, $inner_trait:ident, $inner_type:ident, $constructor:ident) => {
        impl<A: $outer_trait + $inner_trait> $outer_type<$inner_type<A>> {
            doc_comment! {
                concat!("Constructs a new `", stringify!($outer_type), "<", stringify!($inner_type), "<A>>`.

`", stringify!($constructor), "(addr)` is equivalent to `", stringify!($inner_type),
"::new(addr).and_then(", stringify!($outer_type), "::new))`."),
                #[inline]
                pub fn $constructor(addr: A) -> Option<$outer_type<$inner_type<A>>> {
                    $inner_type::new(addr).and_then($outer_type::new)
                }
            }
        }

        impl<A: $outer_trait + $inner_trait> Witness<A> for $outer_type<$inner_type<A>> {
            #[inline]
            fn new(addr: A) -> Option<$outer_type<$inner_type<A>>> {
                $inner_type::new(addr).and_then(Witness::<$inner_type<A>>::new)
            }

            unsafe fn new_unchecked(addr: A) -> $outer_type<$inner_type<A>> {
                $outer_type($inner_type(addr))
            }

            #[inline]
            fn into_addr(self) -> A {
                self.0.into_addr()
            }
        }

        impl<A: $outer_trait + $inner_trait> AsRef<A> for $outer_type<$inner_type<A>> {
            fn as_ref(&self) -> &A {
                &self.0 .0
            }
        }

        impl<A: $outer_trait + $inner_trait> TryFrom<$inner_type<A>> for $outer_type<$inner_type<A>> {
            type Error = ();
            fn try_from(addr: $inner_type<A>) -> Result<$outer_type<$inner_type<A>>, ()> {
                $outer_type::new(addr).ok_or(())
            }
        }
        impl<A: $outer_trait + $inner_trait> TryFrom<$outer_type<A>> for $outer_type<$inner_type<A>> {
            type Error = ();
            fn try_from(addr: $outer_type<A>) -> Result<$outer_type<$inner_type<A>>, ()> {
                // Note that `.map($outer_type)` is sound because we're
                // guaranteed by `addr: $outer_type<A>` that
                // `$inner_type::new(addr.into_addr())` satisfies the
                // `$outer_trait` property.
                $inner_type::new(addr.into_addr()).map($outer_type).ok_or(())
            }
        }
    };
}

/// Implements `From<T> for SpecifiedAddr<A>` where `T` is the nested witness
/// type `$outer_type<$inner_type<A>>`.
macro_rules! impl_into_specified_for_nested_witness {
    ($outer_trait:ident, $outer_type:ident, $inner_trait:ident, $inner_type:ident) => {
        impl<A: $outer_trait + $inner_trait + SpecifiedAddress> From<$outer_type<$inner_type<A>>>
            for SpecifiedAddr<A>
        {
            fn from(addr: $outer_type<$inner_type<A>>) -> SpecifiedAddr<A> {
                SpecifiedAddr(addr.into_addr())
            }
        }
    };
}

/// Implements `TryFrom<$from_ty<A>> for $into_ty<A>`
macro_rules! impl_try_from_witness {
    (@inner [$from_ty:ident: $from_trait:ident], [$into_ty:ident: $into_trait:ident]) => {
        impl<A: $from_trait + $into_trait> TryFrom<$from_ty<A>> for $into_ty<A> {
            type Error = ();
            fn try_from(addr: $from_ty<A>) -> Result<$into_ty<A>, ()> {
                Witness::<A>::from_witness(addr).ok_or(())
            }
        }
    };
    ([$from_ty:ident: $from_trait:ident], $([$into_ty:ident: $into_trait:ident]),*) => {
        $(
            impl_try_from_witness!(@inner [$from_ty: $from_trait], [$into_ty: $into_trait]);
        )*
    }
}

// SpecifiedAddr
impl_witness!(SpecifiedAddr, "specified", SpecifiedAddress, is_specified);
impl_try_from_witness!(
    [SpecifiedAddr: SpecifiedAddress],
    [UnicastAddr: UnicastAddress],
    [MulticastAddr: MulticastAddress],
    [BroadcastAddr: BroadcastAddress],
    [LinkLocalAddr: LinkLocalAddress],
    [LinkLocalUnicastAddr: LinkLocalUnicastAddress],
    [LinkLocalMulticastAddr: LinkLocalMulticastAddress],
    [LinkLocalBroadcastAddr: LinkLocalBroadcastAddress]
);

// UnicastAddr
impl_witness!(UnicastAddr, "unicast", UnicastAddress, is_unicast);
impl_into_specified!(UnicastAddr, UnicastAddress, is_unicast);
impl_nested_witness!(UnicastAddress, UnicastAddr, LinkLocalAddress, LinkLocalAddr, new_link_local);
impl_try_from_witness!(
    [UnicastAddr: UnicastAddress],
    [MulticastAddr: MulticastAddress],
    [BroadcastAddr: BroadcastAddress],
    [LinkLocalAddr: LinkLocalAddress],
    [LinkLocalMulticastAddr: LinkLocalMulticastAddress],
    [LinkLocalBroadcastAddr: LinkLocalBroadcastAddress]
);

// MulticastAddr
impl_witness!(MulticastAddr, "multicast", MulticastAddress, is_multicast);
impl_into_specified!(MulticastAddr, MulticastAddress, is_multicast);
impl_nested_witness!(
    MulticastAddress,
    MulticastAddr,
    LinkLocalAddress,
    LinkLocalAddr,
    new_link_local
);
impl_into_specified_for_nested_witness!(
    MulticastAddress,
    MulticastAddr,
    LinkLocalAddress,
    LinkLocalAddr
);
impl_try_from_witness!(
    [MulticastAddr: MulticastAddress],
    [UnicastAddr: UnicastAddress],
    [BroadcastAddr: BroadcastAddress],
    [LinkLocalAddr: LinkLocalAddress],
    [LinkLocalUnicastAddr: LinkLocalUnicastAddress],
    [LinkLocalBroadcastAddr: LinkLocalBroadcastAddress]
);

// BroadcastAddr
impl_witness!(BroadcastAddr, "broadcast", BroadcastAddress, is_broadcast);
impl_into_specified!(BroadcastAddr, BroadcastAddress, is_broadcast);
impl_nested_witness!(
    BroadcastAddress,
    BroadcastAddr,
    LinkLocalAddress,
    LinkLocalAddr,
    new_link_local
);
impl_into_specified_for_nested_witness!(
    BroadcastAddress,
    BroadcastAddr,
    LinkLocalAddress,
    LinkLocalAddr
);
impl_try_from_witness!(
    [BroadcastAddr: BroadcastAddress],
    [UnicastAddr: UnicastAddress],
    [MulticastAddr: MulticastAddress],
    [LinkLocalAddr: LinkLocalAddress],
    [LinkLocalUnicastAddr: LinkLocalUnicastAddress],
    [LinkLocalMulticastAddr: LinkLocalMulticastAddress]
);

// LinkLocalAddr
impl_witness!(LinkLocalAddr, "link-local", LinkLocalAddress, is_link_local);
impl_into_specified!(LinkLocalAddr, LinkLocalAddress, is_link_local);
impl_nested_witness!(LinkLocalAddress, LinkLocalAddr, UnicastAddress, UnicastAddr, new_unicast);
impl_nested_witness!(
    LinkLocalAddress,
    LinkLocalAddr,
    MulticastAddress,
    MulticastAddr,
    new_multicast
);
impl_nested_witness!(
    LinkLocalAddress,
    LinkLocalAddr,
    BroadcastAddress,
    BroadcastAddr,
    new_broadcast
);
impl_into_specified_for_nested_witness!(
    LinkLocalAddress,
    LinkLocalAddr,
    UnicastAddress,
    UnicastAddr
);
impl_into_specified_for_nested_witness!(
    LinkLocalAddress,
    LinkLocalAddr,
    MulticastAddress,
    MulticastAddr
);
impl_into_specified_for_nested_witness!(
    LinkLocalAddress,
    LinkLocalAddr,
    BroadcastAddress,
    BroadcastAddr
);
impl_try_from_witness!(
    [LinkLocalAddr: LinkLocalAddress],
    [UnicastAddr: UnicastAddress],
    [MulticastAddr: MulticastAddress],
    [BroadcastAddr: BroadcastAddress]
);

// NOTE(joshlf): We provide these type aliases both for convenience and also to
// steer users towards these types and away from `UnicastAddr<LinkLocalAddr<A>>`
// and `MulticastAddr<LinkLocalAddr<A>>`, which are also valid. The reason we
// still implement `Witness<A>` for those types is that user code may contain
// generic contexts (e.g., some code with `UnicastAddr<A>`, and other code which
// wishes to supply `A = LinkLocalAddr<AA>`), and we want to support that use
// case.

/// An address that can be link-local and unicast.
///
/// `LinkLocalUnicastAddress` is a shorthand for `LinkLocalAddress +
/// UnicastAddress`.
pub trait LinkLocalUnicastAddress: LinkLocalAddress + UnicastAddress {}
impl<A: LinkLocalAddress + UnicastAddress> LinkLocalUnicastAddress for A {}

/// An address that can be link-local and multicast.
///
/// `LinkLocalMulticastAddress` is a shorthand for `LinkLocalAddress +
/// MulticastAddress`.
pub trait LinkLocalMulticastAddress: LinkLocalAddress + MulticastAddress {}
impl<A: LinkLocalAddress + MulticastAddress> LinkLocalMulticastAddress for A {}

/// An address that can be link-local and broadcast.
///
/// `LinkLocalBroadcastAddress` is a shorthand for `LinkLocalAddress +
/// BroadcastAddress`.
pub trait LinkLocalBroadcastAddress: LinkLocalAddress + BroadcastAddress {}
impl<A: LinkLocalAddress + BroadcastAddress> LinkLocalBroadcastAddress for A {}

/// A link-local unicast address.
pub type LinkLocalUnicastAddr<A> = LinkLocalAddr<UnicastAddr<A>>;

/// A link-local multicast address.
pub type LinkLocalMulticastAddr<A> = LinkLocalAddr<MulticastAddr<A>>;

/// A link-local broadcast address.
pub type LinkLocalBroadcastAddr<A> = LinkLocalAddr<BroadcastAddr<A>>;

impl_try_from_witness!(
    [LinkLocalUnicastAddr: LinkLocalUnicastAddress],
    [UnicastAddr: UnicastAddress],
    [MulticastAddr: MulticastAddress],
    [LinkLocalAddr: LinkLocalAddress],
    [LinkLocalMulticastAddr: LinkLocalMulticastAddress],
    [LinkLocalBroadcastAddr: LinkLocalBroadcastAddress]
);
impl_try_from_witness!(
    [LinkLocalMulticastAddr: LinkLocalMulticastAddress],
    [UnicastAddr: UnicastAddress],
    [MulticastAddr: MulticastAddress],
    [LinkLocalAddr: LinkLocalAddress],
    [LinkLocalUnicastAddr: LinkLocalUnicastAddress],
    [LinkLocalBroadcastAddr: LinkLocalBroadcastAddress]
);
impl_try_from_witness!(
    [LinkLocalBroadcastAddr: LinkLocalBroadcastAddress],
    [UnicastAddr: UnicastAddress],
    [MulticastAddr: MulticastAddress],
    [LinkLocalAddr: LinkLocalAddress],
    [LinkLocalUnicastAddr: LinkLocalUnicastAddress],
    [LinkLocalMulticastAddr: LinkLocalMulticastAddress]
);

/// A witness type for an address and a scope zone.
///
/// `AddrAndZone` carries an address that *may* have a scope, alongside the
/// particular zone of that scope. The zone is also referred to as a "scope
/// identifier" in some systems (such as Linux).
///
/// Note that although `AddrAndZone` acts as a witness type, it does not
/// implement [`Witness`] since it carries both the address and scoping
/// information, and not only the witnessed address.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub struct AddrAndZone<A, Z>(A, Z);

impl<A: ScopeableAddress, Z> AddrAndZone<A, Z> {
    /// Constructs a new `AddrAndZone`, returning `Some` only if the provided
    /// `addr`'s scope can have a zone (`addr.scope().can_have_zone()`).
    pub fn new(addr: A, zone: Z) -> Option<Self> {
        if addr.scope().can_have_zone() {
            Some(Self(addr, zone))
        } else {
            None
        }
    }

    /// Consumes this `AddrAndZone`, returning the address and zone separately.
    pub fn into_addr_scope_id(self) -> (A, Z) {
        let AddrAndZone(addr, zone) = self;
        (addr, zone)
    }
}

impl<A, Z> AddrAndZone<A, Z> {
    /// Constructs a new `AddrAndZone` without checking to see if `addr`'s scope
    /// can have a zone.
    ///
    /// # Safety
    ///
    /// It is up to the caller to make sure that `addr`'s scope can have a zone
    /// to avoid breaking the guarantees of `AddrAndZone`.
    #[inline]
    pub const unsafe fn new_unchecked(addr: A, zone: Z) -> Self {
        Self(addr, zone)
    }
}

impl<A: ScopeableAddress + SpecifiedAddress, Z> AddrAndZone<A, Z> {
    /// Consumes this `AddrAndZone`, returning the address (as a
    /// [`SpecifiedAddr`]) and zone separately.
    pub fn into_specified_addr_zone(self) -> (SpecifiedAddr<A>, Z) {
        (SpecifiedAddr(self.0), self.1)
    }
}

impl<A: ScopeableAddress + Display, Z: Display> Display for AddrAndZone<A, Z> {
    #[inline]
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "{}%{}", self.0, self.1)
    }
}

impl<A, Z> sealed::Sealed for AddrAndZone<A, Z> {}

/// An address that may have an associated scope zone.
#[allow(missing_docs)]
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub enum ZonedAddr<A, Z> {
    Unzoned(SpecifiedAddr<A>),
    Zoned(AddrAndZone<A, Z>),
}

impl<A: ScopeableAddress + SpecifiedAddress, Z> ZonedAddr<A, Z> {
    /// Creates a new `ZonedAddr` with the provided optional scope zone.
    ///
    /// If `zone` is `None`, [`ZonedAddr::Unzoned`] is returned. Otherwise, a
    /// [`ZonedAddr::Zoned`] is returned only if the provided `addr`'s scope can
    /// have a zone (`addr.scope().can_have_zone()`).
    pub fn new(addr: A, zone: Option<Z>) -> Option<Self> {
        match zone {
            Some(zone) => AddrAndZone::new(addr, zone).map(ZonedAddr::Zoned),
            None => SpecifiedAddr::new(addr).map(ZonedAddr::Unzoned),
        }
    }

    /// Decomposes this `ZonedAddr` into a [`SpecifiedAddr`] and an optional
    /// scope zone.
    pub fn into_addr_zone(self) -> (SpecifiedAddr<A>, Option<Z>) {
        match self {
            ZonedAddr::Unzoned(addr) => (addr, None),
            ZonedAddr::Zoned(scope_and_zone) => {
                let (addr, zone) = scope_and_zone.into_specified_addr_zone();
                (addr, Some(zone))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Copy, Clone, Debug, Eq, PartialEq)]
    enum Address {
        Unspecified,
        GlobalUnicast,
        GlobalMulticast,
        GlobalBroadcast,
        LinkLocalUnicast,
        LinkLocalMulticast,
        LinkLocalBroadcast,
    }

    impl SpecifiedAddress for Address {
        fn is_specified(&self) -> bool {
            *self != Address::Unspecified
        }
    }

    impl UnicastAddress for Address {
        fn is_unicast(&self) -> bool {
            use Address::*;
            match self {
                GlobalUnicast | LinkLocalUnicast => true,
                Unspecified | GlobalMulticast | GlobalBroadcast | LinkLocalMulticast
                | LinkLocalBroadcast => false,
            }
        }
    }

    impl MulticastAddress for Address {
        fn is_multicast(&self) -> bool {
            use Address::*;
            match self {
                GlobalMulticast | LinkLocalMulticast => true,
                Unspecified | GlobalUnicast | GlobalBroadcast | LinkLocalUnicast
                | LinkLocalBroadcast => false,
            }
        }
    }

    impl BroadcastAddress for Address {
        fn is_broadcast(&self) -> bool {
            use Address::*;
            match self {
                GlobalBroadcast | LinkLocalBroadcast => true,
                Unspecified | GlobalUnicast | GlobalMulticast | LinkLocalUnicast
                | LinkLocalMulticast => false,
            }
        }
    }

    impl LinkLocalAddress for Address {
        fn is_link_local(&self) -> bool {
            use Address::*;
            match self {
                LinkLocalUnicast | LinkLocalMulticast | LinkLocalBroadcast => true,
                Unspecified | GlobalUnicast | GlobalMulticast | GlobalBroadcast => false,
            }
        }
    }

    #[derive(Copy, Clone, Eq, PartialEq)]
    enum AddressScope {
        LinkLocal,
        Global,
    }

    impl Scope for AddressScope {
        fn can_have_zone(&self) -> bool {
            matches!(self, AddressScope::LinkLocal)
        }
    }

    impl ScopeableAddress for Address {
        type Scope = AddressScope;

        fn scope(&self) -> AddressScope {
            if self.is_link_local() {
                AddressScope::LinkLocal
            } else {
                AddressScope::Global
            }
        }
    }

    #[test]
    fn test_specified_addr() {
        assert_eq!(
            SpecifiedAddr::new(Address::GlobalUnicast),
            Some(SpecifiedAddr(Address::GlobalUnicast))
        );
        assert_eq!(SpecifiedAddr::new(Address::Unspecified), None);
    }

    #[test]
    fn test_unicast_addr() {
        assert_eq!(
            UnicastAddr::new(Address::GlobalUnicast),
            Some(UnicastAddr(Address::GlobalUnicast))
        );
        assert_eq!(UnicastAddr::new(Address::GlobalMulticast), None);
        assert_eq!(
            unsafe { UnicastAddr::new_unchecked(Address::GlobalUnicast) },
            UnicastAddr(Address::GlobalUnicast)
        );
    }

    #[test]
    fn test_multicast_addr() {
        assert_eq!(
            MulticastAddr::new(Address::GlobalMulticast),
            Some(MulticastAddr(Address::GlobalMulticast))
        );
        assert_eq!(MulticastAddr::new(Address::GlobalUnicast), None);
        assert_eq!(
            unsafe { MulticastAddr::new_unchecked(Address::GlobalMulticast) },
            MulticastAddr(Address::GlobalMulticast)
        );
    }

    #[test]
    fn test_broadcast_addr() {
        assert_eq!(
            BroadcastAddr::new(Address::GlobalBroadcast),
            Some(BroadcastAddr(Address::GlobalBroadcast))
        );
        assert_eq!(BroadcastAddr::new(Address::GlobalUnicast), None);
        assert_eq!(
            unsafe { BroadcastAddr::new_unchecked(Address::GlobalBroadcast) },
            BroadcastAddr(Address::GlobalBroadcast)
        );
    }

    #[test]
    fn test_link_local_addr() {
        assert_eq!(
            LinkLocalAddr::new(Address::LinkLocalUnicast),
            Some(LinkLocalAddr(Address::LinkLocalUnicast))
        );
        assert_eq!(LinkLocalAddr::new(Address::GlobalMulticast), None);
        assert_eq!(
            unsafe { LinkLocalAddr::new_unchecked(Address::LinkLocalUnicast) },
            LinkLocalAddr(Address::LinkLocalUnicast)
        );
    }

    #[test]
    fn test_nested() {
        // Test UnicastAddr<LinkLocalAddr>, MulticastAddr<LinkLocalAddr>,
        // BroadcastAddr<LinkLocalAddr>, LinkLocalAddr<UnicastAddr>,
        // LinkLocalAddr<MulticastAddr>, LinkLocalAddr<BroadcastAddr>.

        macro_rules! test_nested {
            ($new:expr, $($input:ident => $output:expr,)*) => {
                $(
                    assert_eq!($new(Address::$input), $output);
                )*
            };
        }

        // Unicast
        test_nested!(
            UnicastAddr::new_link_local,
            Unspecified => None,
            GlobalUnicast => None,
            GlobalMulticast => None,
            LinkLocalUnicast => Some(UnicastAddr(LinkLocalAddr(Address::LinkLocalUnicast))),
            LinkLocalMulticast => None,
            LinkLocalBroadcast => None,
        );

        // Multicast
        test_nested!(MulticastAddr::new_link_local,
            Unspecified => None,
            GlobalUnicast => None,
            GlobalMulticast => None,
            LinkLocalUnicast => None,
            LinkLocalMulticast => Some(MulticastAddr(LinkLocalAddr(Address::LinkLocalMulticast))),
            LinkLocalBroadcast => None,
        );

        // Broadcast
        test_nested!(BroadcastAddr::new_link_local,
            Unspecified => None,
            GlobalUnicast => None,
            GlobalMulticast => None,
            LinkLocalUnicast => None,
            LinkLocalMulticast => None,
            LinkLocalBroadcast => Some(BroadcastAddr(LinkLocalAddr(Address::LinkLocalBroadcast))),
        );

        // Link-local
        test_nested!(LinkLocalAddr::new_unicast,
            Unspecified => None,
            GlobalUnicast => None,
            GlobalMulticast => None,
            LinkLocalUnicast => Some(LinkLocalAddr(UnicastAddr(Address::LinkLocalUnicast))),
            LinkLocalMulticast => None,
            LinkLocalBroadcast => None,
        );
        test_nested!(LinkLocalAddr::new_multicast,
            Unspecified => None,
            GlobalUnicast => None,
            GlobalMulticast => None,
            LinkLocalUnicast => None,
            LinkLocalMulticast => Some(LinkLocalAddr(MulticastAddr(Address::LinkLocalMulticast))),
            LinkLocalBroadcast => None,
        );
        test_nested!(LinkLocalAddr::new_broadcast,
            Unspecified => None,
            GlobalUnicast => None,
            GlobalMulticast => None,
            LinkLocalUnicast => None,
            LinkLocalMulticast => None,
            LinkLocalBroadcast => Some(LinkLocalAddr(BroadcastAddr(Address::LinkLocalBroadcast))),
        );
    }

    #[test]
    fn test_addr_and_zone() {
        let addr_and_zone = AddrAndZone::new(Address::LinkLocalUnicast, ());
        assert_eq!(addr_and_zone, Some(AddrAndZone(Address::LinkLocalUnicast, ())));
        assert_eq!(addr_and_zone.unwrap().into_addr_scope_id(), (Address::LinkLocalUnicast, ()));
        assert_eq!(AddrAndZone::new(Address::GlobalUnicast, ()), None);
        assert_eq!(
            unsafe { AddrAndZone::new_unchecked(Address::LinkLocalUnicast, ()) },
            AddrAndZone(Address::LinkLocalUnicast, ())
        );
    }

    #[test]
    fn test_scoped_address() {
        // Type alias to help the compiler when the scope type can't be
        // inferred.
        type ZonedAddr = crate::ZonedAddr<Address, ()>;
        assert_eq!(
            ZonedAddr::new(Address::GlobalUnicast, None),
            Some(ZonedAddr::Unzoned(SpecifiedAddr(Address::GlobalUnicast)))
        );
        assert_eq!(ZonedAddr::new(Address::Unspecified, None), None);
        assert_eq!(
            ZonedAddr::new(Address::LinkLocalUnicast, None),
            Some(ZonedAddr::Unzoned(SpecifiedAddr(Address::LinkLocalUnicast)))
        );
        assert_eq!(ZonedAddr::new(Address::GlobalUnicast, Some(())), None);
        assert_eq!(ZonedAddr::new(Address::Unspecified, Some(())), None);
        assert_eq!(
            ZonedAddr::new(Address::LinkLocalUnicast, Some(())),
            Some(ZonedAddr::Zoned(AddrAndZone(Address::LinkLocalUnicast, ())))
        );

        assert_eq!(
            ZonedAddr::new(Address::GlobalUnicast, None).unwrap().into_addr_zone(),
            (SpecifiedAddr(Address::GlobalUnicast), None)
        );
        assert_eq!(
            ZonedAddr::new(Address::LinkLocalUnicast, Some(())).unwrap().into_addr_zone(),
            (SpecifiedAddr(Address::LinkLocalUnicast), Some(()))
        );
    }
}
