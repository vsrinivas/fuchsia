// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Macros used in `recovery_netstack`.

/// Define a function which is specialized on [`ip::Ipv4`] and [`ip::Ipv6`].
///
/// `specialize_ip` is used to define a generic function which has specialized
/// implementations depending on which IP version is used - `Ipv4` or `Ipv6`.
/// The caller provides the generic function declaration, using `Self` in place
/// of a concrete `Ip` type, and provides the bodies of the two specializations
/// of the function.
///
/// In the scope of the macro invocation, the function is added to the `Ip`
/// trait. Thus, given a function name `foo`, it can be invoked as either
/// `Ipv4::foo()` or `Ipv6::foo()` (or, for a type parameter `I: Ip`,
/// `I::foo()`).
///
/// # Examples
///
/// ```rust
/// pub struct IpState {
///     ipv4_table: ForwardingTable<Ipv4>,
///     ipv6_table: ForwardingTable<Ipv6>,
/// }
///
/// fn get_routing_table<I: Ip>(state: &mut StackState) -> &mut ForwardingTable<I> {
///     // For the scope of this function body, get_routing_table is
///     // available for the trait Ip.
///     specialize_ip!(fn get_routing_table(state: &mut StackState) -> &mut ForwardingTable<Self> {
///         Ipv4 => { &mut state.ip.ipv4_table }
///         Ipv6 => { &mut state.ip.ipv6_table }
///     });
///     I::get_routing_table(state)
/// }
/// ```
///
/// # Limitations
///
/// Due to limitations in Rust's macro system, this macro has the following
/// limitations:
///
/// - Type bounds are not supported using the syntax `fn foo<T: Debug>(t: T)`.
///   Instead, use a `where` clause, as in `fn foo<T>(t: T) where T: Debug`.
/// - Type bounds in `where` clauses cannot contain type parameters. In other
///   words, `where T: Debug` is allowed, but `where T: AsRef<U>` is not.
/// - Type bounds in `where` clauses cannot contain paths. In other words,
///   `where T: Debug` is allowed, but `where T: ::std::fmt::Debug` is not.
#[macro_export] // So that it is documented in 'cargo rustdoc'
macro_rules! specialize_ip {
    // With type arguments, a return type, and a where clause. All other matches
    // with type arguments are implemented in terms of this match.
    (
        fn $fn_name:ident<$($ty_name:tt),*>($($arg_name:ident : $arg_ty:ty),*) -> $ret:ty
        where
            $($where_ty:ty : $where_bound_first:ident $(+ $where_bound_rest:ident)*,)*
        {
            Ipv4 => $ipv4:block
            Ipv6 => $ipv6:block
        }
    ) => (
        specialize_ip_inner!(
            ::ip::Ip;
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            {
                ::ip::Ipv4 => $ipv4
                ::ip::Ipv6 => $ipv6
            }
        );
    );
    // with type arguments, a return type, and no where clause
    (
        fn $fn_name:ident<$($ty_name:tt),*>($($arg_name:ident : $arg_ty:ty),*) -> $ret:ty
        {
            Ipv4 => $ipv4:block
            Ipv6 => $ipv6:block
        }
    ) => (
        specialize_ip!(
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> $ret where {
                Ipv4 => $ipv4
                Ipv6 => $ipv6
            }
        );
    );
    // with type arguments, no return type, and a where clause
    (
        fn $fn_name:ident<$($ty_name:tt),*>($($arg_name:ident : $arg_ty:ty),*)
        where
            $($where_ty:ty : $where_bound_first:ident $(+ $where_bound_rest:ident)*,)*
        {
            Ipv4 => $ipv4:block
            Ipv6 => $ipv6:block
        }
    ) => (
        specialize_ip!(
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> ()
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            {
                Ipv4 => $ipv4
                Ipv6 => $ipv6
            }
        );
    );
    // with type arguments, no return type, and no where clause
    (
        fn $fn_name:ident<$($ty_name:tt),*>($($arg_name:ident : $arg_ty:ty),*) {
            Ipv4 => $ipv4:block
            Ipv6 => $ipv6:block
        }
    ) => (
        specialize_ip!(
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> () where {
                Ipv4 => $ipv4
                Ipv6 => $ipv6
            }
        );
    );
    // With no type arguments, a return type, and a where clause. All other
    // matches without type arguments are implemented in terms of this match.
    // This needs to call specialize_ip_inner directly without any type
    // arguments (as opposed to being implemented in terms of a match which
    // accepts type arguments and just passing <>) because <> isn't valid for
    // <$($ty_name:tt),*> because there's a parsing ambiguity as to whether the
    // trailing > matches the literal > or a tt.
    (
        fn $fn_name:ident($($arg_name:ident : $arg_ty:ty),*) -> $ret:ty
        where
            $($where_ty:ty : $where_bound_first:ident $(+ $where_bound_rest:ident)*,)*
        {
            Ipv4 => $ipv4:block
            Ipv6 => $ipv6:block
        }
    ) => (
        specialize_ip_inner!(
            ::ip::Ip;
            fn $fn_name($($arg_name: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            {
                ::ip::Ipv4 => $ipv4
                ::ip::Ipv6 => $ipv6
            }
        );
    );
    // with no type arguments, a return type, and no where clause
    (
        fn $fn_name:ident($($arg_name:ident : $arg_ty:ty),*) -> $ret:ty {
            Ipv4 => $ipv4:block
            Ipv6 => $ipv6:block
        }
    ) => (
        specialize_ip!(
            fn $fn_name($($arg_name: $arg_ty),*) -> $ret where {
                Ipv4 => $ipv4
                Ipv6 => $ipv6
            }
        );
    );
    // with no type arguments, no return type, and a where clause
    (
        fn $fn_name:ident($($arg_name:ident : $arg_ty:ty),*)
        where
            $($where_ty:ty : $where_bound_first:ident $(+ $where_bound_rest:ident)*,)*
        {
            Ipv4 => $ipv4:block
            Ipv6 => $ipv6:block
        }
    ) => (
        specialize_ip!(
            fn $fn_name($($arg_name: $arg_ty),*) -> ()
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            {
                Ipv4 => $ipv4
                Ipv6 => $ipv6
            }
        );
    );
    // with no type arguments, no return type, and no where clause
    (
        fn $fn_name:ident($($arg_name:ident : $arg_ty:ty),*) {
            Ipv4 => $ipv4:block
            Ipv6 => $ipv6:block
        }
    ) => (
        specialize_ip!(
            fn $fn_name($($arg_name: $arg_ty),*) -> () where {
                Ipv4 => $ipv4
                Ipv6 => $ipv6
            }
        );
    );
}

/// Define a function which is specialized on [`ip::Ipv4Addr`] and
/// [`ip::Ipv6Addr`].
///
/// `specialize_ip_addr` is used to define a generic function which has
/// specialized implementations depending on which IP address version is used -
/// `Ipv4Addr` or `Ipv6Addr`. The caller provides the generic function
/// declaration, using `Self` in place of a concrete `IpAddr` type, and provides
/// the bodies of the two specializations of the function.
///
/// In the scope of the macro invocation, the function is added to the `IpAddr`
/// trait. Thus, given a function name `foo`, it can be invoked as either
/// `Ipv4Addr::foo()` or `Ipv6Addr::foo()` (or, for a type parameter `A:
/// IpAddr`, `A::foo()`).
///
/// # Examples
///
/// ```rust
/// pub struct EthernetDeviceState {
///     ipv4_addr: Option<Ipv4Addr>,
///     ipv6_addr: Option<Ipv6Addr>,
/// }
///
/// fn get_ip_addr<A: IpAddr>(state: &EthernetDeviceState) -> Option<A> {
///     // For the scope of this function body, get_ip_addr is
///     // available for the trait IpAddr.
///     specialize_ip_addr!(fn get_ip_addr(state: &EthernetDeviceState) -> Option<Self> {
///         Ipv4 => { state.ipv4_addr }
///         Ipv6 => { state.ipv6_addr }
///     });
///     A::get_ip_addr(state)
/// }
/// ```
///
/// # Limitations
///
/// Due to limitations in Rust's macro system, this macro has the following
/// limitations:
///
/// - Type bounds are not supported using the syntax `fn foo<T: Debug>(t: T)`.
///   Instead, use a `where` clause, as in `fn foo<T>(t: T) where T: Debug`.
/// - Type bounds in `where` clauses cannot contain type parameters. In other
///   words, `where T: Debug` is allowed, but `where T: AsRef<U>` is not.
/// - Type bounds in `where` clauses cannot contain paths. In other words,
///   `where T: Debug` is allowed, but `where T: ::std::fmt::Debug` is not.
#[macro_export] // So that it is documented in 'cargo rustdoc'
macro_rules! specialize_ip_addr {
    // With type arguments, a return type, and a where clause. All other matches
    // with type arguments are implemented in terms of this match.
    (
        fn $fn_name:ident<$($ty_name:tt),*>($($arg_name:ident : $arg_ty:ty),*) -> $ret:ty
        where
            $($where_ty:ty : $where_bound_first:ident $(+ $where_bound_rest:ident)*,)*
        {
            Ipv4Addr => $ipv4:block
            Ipv6Addr => $ipv6:block
        }
    ) => (
        specialize_ip_inner!(
            ::ip::IpAddr;
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            {
                ::ip::Ipv4Addr => $ipv4
                ::ip::Ipv6Addr => $ipv6
            }
        );
    );
    // with type arguments, a return type, and no where clause
    (
        fn $fn_name:ident<$($ty_name:tt),*>($($arg_name:ident : $arg_ty:ty),*) -> $ret:ty
        {
            Ipv4Addr => $ipv4:block
            Ipv6Addr => $ipv6:block
        }
    ) => (
        specialize_ip_addr!(
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> $ret where {
                Ipv4Addr => $ipv4
                Ipv6Addr => $ipv6
            }
        );
    );
    // with type arguments, no return type, and a where clause
    (
        fn $fn_name:ident<$($ty_name:tt),*>($($arg_name:ident : $arg_ty:ty),*)
        where
            $($where_ty:ty : $where_bound_first:ident $(+ $where_bound_rest:ident)*,)*
        {
            Ipv4Addr => $ipv4:block
            Ipv6Addr => $ipv6:block
        }
    ) => (
        specialize_ip_addr!(
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> ()
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            {
                Ipv4Addr => $ipv4
                Ipv6Addr => $ipv6
            }
        );
    );
    // with type arguments, no return type, and no where clause
    (
        fn $fn_name:ident<$($ty_name:tt),*>($($arg_name:ident : $arg_ty:ty),*) {
            Ipv4Addr => $ipv4:block
            Ipv6Addr => $ipv6:block
        }
    ) => (
        specialize_ip_addr!(
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> () where {
                Ipv4Addr => $ipv4
                Ipv6Addr => $ipv6
            }
        );
    );
    // With no type arguments, a return type, and a where clause. All other
    // matches without type arguments are implemented in terms of this match.
    // This needs to call specialize_ip_inner directly without any type
    // arguments (as opposed to being implemented in terms of a match which
    // accepts type arguments and just passing <>) because <> isn't valid for
    // <$($ty_name:tt),*> because there's a parsing ambiguity as to whether the
    // trailing > matches the literal > or a tt.
    (
        fn $fn_name:ident($($arg_name:ident : $arg_ty:ty),*) -> $ret:ty
        where
            $($where_ty:ty : $where_bound_first:ident $(+ $where_bound_rest:ident)*,)*
        {
            Ipv4Addr => $ipv4:block
            Ipv6Addr => $ipv6:block
        }
    ) => (
        specialize_ip_inner!(
            ::ip::IpAddr;
            fn $fn_name($($arg_name: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            {
                ::ip::Ipv4Addr => $ipv4
                ::ip::Ipv6Addr => $ipv6
            }
        );
    );
    // with no type arguments, a return type, and no where clause
    (
        fn $fn_name:ident($($arg_name:ident : $arg_ty:ty),*) -> $ret:ty {
            Ipv4Addr => $ipv4:block
            Ipv6Addr => $ipv6:block
        }
    ) => (
        specialize_ip_addr!(
            fn $fn_name($($arg_name: $arg_ty),*) -> $ret where {
                Ipv4Addr => $ipv4
                Ipv6Addr => $ipv6
            }
        );
    );
    // with no type arguments, no return type, and a where clause
    (
        fn $fn_name:ident($($arg_name:ident : $arg_ty:ty),*)
        where
            $($where_ty:ty : $where_bound_first:ident $(+ $where_bound_rest:ident)*,)*
        {
            Ipv4Addr => $ipv4:block
            Ipv6Addr => $ipv6:block
        }
    ) => (
        specialize_ip_addr!(
            fn $fn_name($($arg_name: $arg_ty),*) -> ()
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            {
                Ipv4Addr => $ipv4
                Ipv6Addr => $ipv6
            }
        );
    );
    // with no type arguments, no return type, and no where clause
    (
        fn $fn_name:ident($($arg_name:ident : $arg_ty:ty),*) {
            Ipv4Addr => $ipv4:block
            Ipv6Addr => $ipv6:block
        }
    ) => (
        specialize_ip_addr!(
            fn $fn_name($($arg_name: $arg_ty),*) -> () where {
                Ipv4Addr => $ipv4
                Ipv6Addr => $ipv6
            }
        );
    );
}

#[doc(hidden)]
macro_rules! specialize_ip_inner {
    // with type arguments
    (
        $trait:path;
        fn $fn_name:ident<$($ty_name:tt),*>($($arg_name:ident : $arg_ty:ty),*) -> $ret:ty
        where
            $($where_ty:ty : $where_bound_first:ident $(+ $where_bound_rest:ident)*,)*
        {
            $type_a:path => $block_a:block
            $type_b:path => $block_b:block
        }
    ) => (
        // use the function name as the trait name so that we get collision-free
        // naming
        #[allow(non_camel_case_types)]
        trait $fn_name: $trait {
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*;
        }
        impl<A: $trait> $fn_name for A {
            #[allow(unused)]
            default fn $fn_name<$($ty_name),*>($(_: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            { unreachable!() }
        }
        impl $fn_name for $type_a {
            #[allow(unused)] // in case not all arguments are used in this specialization
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            $block_a
        }
        impl $fn_name for $type_b {
            #[allow(unused)] // in case not all arguments are used in this specialization
            fn $fn_name<$($ty_name),*>($($arg_name: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            $block_b
        }
    );
    // With no type arguments, a return type, and a where clause. All other
    // matches without type arguments are implemented in terms of this match.
    // This needs to call specialize_ip_inner directly without any type
    // arguments (as opposed to being implemented in terms of a match which
    // accepts type arguments and just passing <>) because <> isn't valid for
    // <$($ty_name:tt),*> because there's a parsing ambiguity as to whether the
    // trailing > matches the literal > or a tt.
    (
        $trait:path;
        fn $fn_name:ident($($arg_name:ident : $arg_ty:ty),*) -> $ret:ty
        where
            $($where_ty:ty : $where_bound_first:ident $(+ $where_bound_rest:ident)*,)*
        {
            $type_a:path => $block_a:block
            $type_b:path => $block_b:block
        }
    ) => (
        // use the function name as the trait name so that we get collision-free
        // naming
        #[allow(non_camel_case_types)]
        trait $fn_name: $trait {
            fn $fn_name($($arg_name: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*;
        }
        impl<A: $trait> $fn_name for A {
            default fn $fn_name($(_: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            { unreachable!() }
        }
        impl $fn_name for $type_a {
            fn $fn_name($($arg_name: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            $block_a
        }
        impl $fn_name for $type_b {
            fn $fn_name($($arg_name: $arg_ty),*) -> $ret
            where
                $($where_ty: $where_bound_first $(+ $where_bound_rest)*,)*
            $block_b
        }
    )
}

macro_rules! log_unimplemented {
    ($nocrash:expr, $fmt:expr $(,$arg:expr)*) => {{

        #[cfg(feature = "crash_on_unimplemented")]
        unimplemented!($fmt, $($arg),*);

        #[cfg(not(feature = "crash_on_unimplemented"))]
        {
          trace!(concat!("Unimplemented: ", $fmt), $($arg),*);
          $nocrash
        }
    }};

    ($nocrash:expr, $fmt:expr $(,$arg:expr)*,) =>{
      log_unimplemented!($nocrash, $fmt $(,$arg)*)
    };
}

macro_rules! increment_counter {
    ($state:ident, $key:expr) => {
        #[cfg(test)]
        $state.test_counters.increment($key);
    };
}

mod test {
    // don't 'use' anything from the ip module so we can be sure that the
    // absolute paths used in the definitions of these macros work properly

    use std::fmt::Debug;

    specialize_ip!(
        fn _ip_types_ret_where<T>(t: T, a: Self) -> (T, Self)
        where
            T: Debug,
        {
            Ipv4 => { (t, if true { a } else { ::ip::Ipv4 }) }
            Ipv6 => { (t, if true { a } else { ::ip::Ipv6 }) }
        }
    );
    specialize_ip!(
        fn _ip_types_ret<T>(t: T, a: Self) -> (T, Self) {
            Ipv4 => { (t, if true { a } else { ::ip::Ipv4 }) }
            Ipv6 => { (t, if true { a } else { ::ip::Ipv6 }) }
        }
    );
    specialize_ip!(
        fn _ip_types_where<T>(_t: T, _a: Self) -> ()
        where
            T: Debug,
        {
            Ipv4 => { }
            Ipv6 => { }
        }
    );
    specialize_ip!(
        fn _ip_types<T>(_t: T, _a: Self) {
            Ipv4 => { }
            Ipv6 => { }
        }
    );
    specialize_ip!(
        fn _ip_ret_where(a: Self) -> Self where {
            Ipv4 => { if true { a } else { ::ip::Ipv4 } }
            Ipv6 => { if true { a } else { ::ip::Ipv6 } }
        }
    );
    specialize_ip!(
        fn _ip_ret(a: Self) -> Self {
            Ipv4 => { if true { a } else { ::ip::Ipv4 } }
            Ipv6 => { if true { a } else { ::ip::Ipv6 } }
        }
    );
    specialize_ip!(
        fn _ip_where(_a: Self) where {
            Ipv4 => { }
            Ipv6 => { }
        }
    );
    specialize_ip!(
        fn _ip(_a: Self) {
            Ipv4 => { }
            Ipv6 => { }
        }
    );

    specialize_ip_addr!(
        fn _ip_addr_types_ret_where<T>(t: T, a: Self) -> (T, Self)
        where
            T: Debug,
        {
            Ipv4Addr => { (t, if true { a } else { use ip::Ip; ::ip::Ipv4::LOOPBACK_ADDRESS }) }
            Ipv6Addr => { (t, if true { a } else { use ip::Ip; ::ip::Ipv6::LOOPBACK_ADDRESS }) }
        }
    );
    specialize_ip_addr!(
        fn _ip_addr_types_ret<T>(t: T, a: Self) -> (T, Self) {
            Ipv4Addr => { (t, if true { a } else { use ip::Ip; ::ip::Ipv4::LOOPBACK_ADDRESS }) }
            Ipv6Addr => { (t, if true { a } else { use ip::Ip; ::ip::Ipv6::LOOPBACK_ADDRESS }) }
        }
    );
    specialize_ip_addr!(
        fn _ip_addr_types_where<T>(_t: T, _a: Self) -> ()
        where
            T: Debug,
        {
            Ipv4Addr => { }
            Ipv6Addr => { }
        }
    );
    specialize_ip_addr!(
        fn _ip_addr_types<T>(_t: T, _a: Self) {
            Ipv4Addr => { }
            Ipv6Addr => { }
        }
    );
    specialize_ip_addr!(
        fn _ip_addr_ret_where(a: Self) -> Self where {
            Ipv4Addr => { if true { a } else { use ip::Ip; ::ip::Ipv4::LOOPBACK_ADDRESS } }
            Ipv6Addr => { if true { a } else { use ip::Ip; ::ip::Ipv6::LOOPBACK_ADDRESS } }
        }
    );
    specialize_ip_addr!(
        fn _ip_addr_ret(a: Self) -> Self {
            Ipv4Addr => { if true { a } else { use ip::Ip; ::ip::Ipv4::LOOPBACK_ADDRESS } }
            Ipv6Addr => { if true { a } else { use ip::Ip; ::ip::Ipv6::LOOPBACK_ADDRESS } }
        }
    );
    specialize_ip_addr!(
        fn _ip_addr_where(_a: Self) where {
            Ipv4Addr => { }
            Ipv6Addr => { }
        }
    );
    specialize_ip_addr!(
        fn _ip_addr(_a: Self) {
            Ipv4Addr => { }
            Ipv6Addr => { }
        }
    );
}
