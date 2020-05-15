// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;
#[macro_use]
extern crate quote;

use proc_macro2::TokenStream;
use proc_macro_hack::proc_macro_hack;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6};
use std::str::FromStr;

/// Declares a proc_macro with `name` using `generator` to generate any of `ty`.
macro_rules! declare_macro {
    ($name:ident, $generator:ident, $($ty:ident),+) => {
        #[proc_macro_hack]
        pub fn $name(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
            Emitter::<$generator, $($ty),+>::emit(input).into()
        }
    }
}

/// Empty slot in an [`Emitter`].
struct Skip;

impl FromStr for Skip {
    type Err = ();

    fn from_str(_s: &str) -> Result<Self, Self::Err> {
        Err(())
    }
}

/// Generates a [`TokenStream`] representation of `T`.
trait Generator<T> {
    /// Generates the [`TokenStream`] for `input`.
    fn generate(input: T) -> TokenStream;

    /// Get an optional string representation of type `T`.
    ///
    /// Used in error reporting when parsing fails.
    fn type_str() -> Option<&'static str> {
        Some(std::any::type_name::<T>())
    }
}

impl<O> Generator<Skip> for O {
    fn generate(_input: Skip) -> TokenStream {
        unreachable!()
    }

    fn type_str() -> Option<&'static str> {
        None
    }
}

/// Attempts to emit the resulting [`TokenStream`] for `input` parsed as `T`.
fn try_emit<G, T>(input: &str) -> Result<TokenStream, T::Err>
where
    G: Generator<T>,
    T: FromStr,
{
    Ok(G::generate(T::from_str(input)?))
}

/// Provides common structor for parsing types from [`TokenStream`]s and
/// emitting the resulting [`TokenStream`].
///
/// `Emitter` can parse any of `Tn` and pass into a [`Generator`] `G`, encoding
/// the common logic for declarations build from string parsing at compile-time.
struct Emitter<G, T1 = Skip, T2 = Skip, T3 = Skip, T4 = Skip> {
    _g: std::marker::PhantomData<G>,
    _t1: std::marker::PhantomData<T1>,
    _t2: std::marker::PhantomData<T2>,
    _t3: std::marker::PhantomData<T3>,
    _t4: std::marker::PhantomData<T4>,
}

impl<G, T1, T2, T3, T4> Emitter<G, T1, T2, T3, T4>
where
    G: Generator<T1> + Generator<T2> + Generator<T3> + Generator<T4>,
    T1: FromStr,
    T2: FromStr,
    T3: FromStr,
    T4: FromStr,
{
    /// Emits the resulting [`TokenStream`] (or an error one) after attempting
    /// to parse `input` into all of the `Emitter`'s types sequentially.
    fn emit(input: proc_macro::TokenStream) -> TokenStream {
        let s = format!("{}", input).replace(" ", "");
        match try_emit::<G, T1>(&s)
            .or_else(|_| try_emit::<G, T2>(&s))
            .or_else(|_| try_emit::<G, T3>(&s))
            .or_else(|_| try_emit::<G, T4>(&s))
        {
            Ok(ts) => ts,
            Err(_e) => syn::Error::new_spanned(
                proc_macro2::TokenStream::from(input),
                format!("failed to parse as {}", Self::error_str()),
            )
            .to_compile_error()
            .into(),
        }
    }

    /// Get the error string reported to the compiler when parsing fails with
    /// this `Emitter`.
    fn error_str() -> String {
        [
            <G as Generator<T1>>::type_str(),
            <G as Generator<T2>>::type_str(),
            <G as Generator<T3>>::type_str(),
            <G as Generator<T4>>::type_str(),
        ]
        .iter()
        .filter_map(|x| x.clone())
        .collect::<Vec<_>>()
        .join(" or ")
    }
}

/// Generator for `std` types.
enum StdGen {}

impl Generator<IpAddr> for StdGen {
    fn generate(input: IpAddr) -> TokenStream {
        let (t, inner) = match input {
            IpAddr::V4(v4) => (quote! { V4 }, Self::generate(v4)),
            IpAddr::V6(v6) => (quote! { V6 }, Self::generate(v6)),
        };
        quote! {
            std::net::IpAddr::#t(#inner)
        }
    }
}

impl Generator<Ipv4Addr> for StdGen {
    fn generate(input: Ipv4Addr) -> TokenStream {
        let octets = input.octets();
        quote! {
            std::net::Ipv4Addr::new(#(#octets),*)
        }
    }
}

impl Generator<Ipv6Addr> for StdGen {
    fn generate(input: Ipv6Addr) -> TokenStream {
        let segments = input.segments();
        quote! {
            std::net::Ipv6Addr::new(#(#segments),*)
        }
    }
}

impl Generator<SocketAddr> for StdGen {
    fn generate(input: SocketAddr) -> TokenStream {
        let (t, inner) = match input {
            SocketAddr::V4(v4) => (quote! { V4 }, Self::generate(v4)),
            SocketAddr::V6(v6) => (quote! { V6 }, Self::generate(v6)),
        };
        quote! {
            std::net::SocketAddr::#t(#inner)
        }
    }
}

impl Generator<SocketAddrV4> for StdGen {
    fn generate(input: SocketAddrV4) -> TokenStream {
        let addr = Self::generate(input.ip().clone());
        let port = input.port();
        quote! {
            std::net::SocketAddrV4::new(#addr, #port)
        }
    }
}

impl Generator<SocketAddrV6> for StdGen {
    fn generate(input: SocketAddrV6) -> TokenStream {
        let addr = Self::generate(input.ip().clone());
        let port = input.port();
        let flowinfo = input.flowinfo();
        let scope_id = input.scope_id();
        quote! {
            std::net::SocketAddrV6::new(#addr, #port, #flowinfo, #scope_id)
        }
    }
}

declare_macro!(std_ip, StdGen, IpAddr);
declare_macro!(std_ip_v4, StdGen, Ipv4Addr);
declare_macro!(std_ip_v6, StdGen, Ipv6Addr);
declare_macro!(std_socket_addr, StdGen, SocketAddr, SocketAddrV4);
declare_macro!(std_socket_addr_v4, StdGen, SocketAddrV4);
declare_macro!(std_socket_addr_v6, StdGen, SocketAddrV6);

/// Generator for FIDL types.
enum FidlGen {}

impl Generator<IpAddr> for FidlGen {
    fn generate(input: IpAddr) -> TokenStream {
        let (t, inner) = match input {
            IpAddr::V4(v4) => (quote! { Ipv4 }, Self::generate(v4)),
            IpAddr::V6(v6) => (quote! { Ipv6 }, Self::generate(v6)),
        };
        quote! {
            fidl_fuchsia_net::IpAddress::#t(#inner)
        }
    }
}

impl Generator<Ipv4Addr> for FidlGen {
    fn generate(input: Ipv4Addr) -> TokenStream {
        let octets = input.octets();
        quote! {
            fidl_fuchsia_net::Ipv4Address{ addr: [#(#octets),*]}
        }
    }
}

impl Generator<Ipv6Addr> for FidlGen {
    fn generate(input: Ipv6Addr) -> TokenStream {
        let octets = input.octets();
        quote! {
            fidl_fuchsia_net::Ipv6Address{ addr: [#(#octets),*]}
        }
    }
}

impl Generator<SocketAddr> for FidlGen {
    fn generate(input: SocketAddr) -> TokenStream {
        let (t, inner) = match input {
            SocketAddr::V4(v4) => (quote! { Ipv4 }, Self::generate(v4)),
            SocketAddr::V6(v6) => (quote! { Ipv6 }, Self::generate(v6)),
        };
        quote! {
            fidl_fuchsia_net::SocketAddress::#t(#inner)
        }
    }
}

impl Generator<SocketAddrV4> for FidlGen {
    fn generate(input: SocketAddrV4) -> TokenStream {
        let addr = Self::generate(input.ip().clone());
        let port = input.port();
        quote! {
            fidl_fuchsia_net::Ipv4SocketAddress {
                address: #addr,
                port: #port
            }
        }
    }
}

impl Generator<SocketAddrV6> for FidlGen {
    fn generate(input: SocketAddrV6) -> TokenStream {
        let addr = Self::generate(input.ip().clone());
        let port = input.port();
        let scope_id = u64::from(input.scope_id());
        quote! {
            fidl_fuchsia_net::Ipv6SocketAddress {
                address: #addr,
                port: #port,
                zone_index: #scope_id
            }
        }
    }
}

declare_macro!(fidl_ip, FidlGen, IpAddr);
declare_macro!(fidl_ip_v4, FidlGen, Ipv4Addr);
declare_macro!(fidl_ip_v6, FidlGen, Ipv6Addr);
declare_macro!(fidl_socket_addr, FidlGen, SocketAddr);
declare_macro!(fidl_socket_addr_v4, FidlGen, SocketAddrV4);
declare_macro!(fidl_socket_addr_v6, FidlGen, SocketAddrV6);
