// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Module with types that have the same identifiers that `ip_test` and
/// `ip_addr_test` attributes looks for. Both of these attributes
/// look for the `Ipv4`, `Ipv6`, `Ipv4Addr`, and `Ipv6Addr` identifiers
/// within `net_types::ip` so we create the same hierarchy in this test.
#[cfg(test)]
pub(crate) mod net_types {
    pub(crate) mod ip {
        /// Simple trait with the identifier `Ip` to test that the `ip_test`
        /// attribute specializes function for the various types that implement
        /// this trait.
        pub(crate) trait Ip {
            type Addr: IpAddress;
            const VERSION: usize;
        }

        /// Simple trait with the identifier `IpAddress` to test that the
        /// `ip_test` attribute specializes function for the various types that
        /// implement this trait.
        pub(crate) trait IpAddress {
            const VERSION: usize;
            fn new() -> Self;
        }

        /// Test type with the identifier `Ipv4` that the `ip_test` attribute
        /// can specialize a function for.
        pub(crate) struct Ipv4;
        impl Ip for Ipv4 {
            type Addr = Ipv4Addr;
            const VERSION: usize = 4;
        }

        /// Test type with the identifier `Ipv6` that the `ip_test` attribute
        /// can specialize a function for.
        pub(crate) struct Ipv6;
        impl Ip for Ipv6 {
            type Addr = Ipv6Addr;
            const VERSION: usize = 6;
        }

        /// Test type with the identifier `Ipv4Addr` that the `ip_addr_test`
        /// attribute can specialize a function for.
        pub(crate) struct Ipv4Addr;
        impl IpAddress for Ipv4Addr {
            const VERSION: usize = 4;
            fn new() -> Self {
                Self
            }
        }

        /// Test type with the identifier `Ipv6Addr` that the `ip_addr_test`
        /// attribute can specialize a function for.
        pub(crate) struct Ipv6Addr;
        impl IpAddress for Ipv6Addr {
            const VERSION: usize = 6;
            fn new() -> Self {
                Self
            }
        }
    }
}

#[cfg(test)]
mod test {
    use crate::net_types::ip::{Ip, IpAddress, Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
    use ip_test_macro::{ip_addr_test, ip_test};

    trait IpExt {
        const SIMPLE_VALUE: u8;
    }

    impl IpExt for Ipv4 {
        const SIMPLE_VALUE: u8 = 1;
    }

    impl IpExt for Ipv6 {
        const SIMPLE_VALUE: u8 = 2;
    }

    trait IpAddressExt {
        const SIMPLE_VALUE: u16;
    }

    impl IpAddressExt for Ipv4Addr {
        const SIMPLE_VALUE: u16 = 3;
    }

    impl IpAddressExt for Ipv6Addr {
        const SIMPLE_VALUE: u16 = 4;
    }

    fn simple_specialized_for_ip<I: IpExt>() -> u8 {
        I::SIMPLE_VALUE
    }

    fn simple_specialized_for_ip_address<A: IpAddressExt>() -> u16 {
        A::SIMPLE_VALUE
    }

    #[ip_test]
    fn test_ip_test<I: Ip + IpExt>() {
        let x = simple_specialized_for_ip::<I>();
        if I::VERSION == 4 {
            assert!(x == 1);
        } else {
            assert!(x == 2);
        }
    }

    #[ip_addr_test]
    fn test_ip_addr_test<A: IpAddress + IpAddressExt>() {
        let x = simple_specialized_for_ip_address::<A>();
        if A::VERSION == 4 {
            assert!(x == 3);
        } else {
            assert!(x == 4);
        }
    }

    // test that all the `ip_[addr]_test` functions were generated
    #[test]
    fn test_all_tests_generation() {
        test_ip_test_v4();
        test_ip_test_v6();
        test_ip_addr_test_v4();
        test_ip_addr_test_v6();
    }

    #[ip_test]
    #[should_panic]
    fn test_should_panic<I: Ip + IpExt>() {
        assert_eq!(0, simple_specialized_for_ip::<I>());
    }

    mod real_test_case {
        use ::test_case::test_case;

        use super::*;

        #[ip_test]
        #[test_case(true, "bool")]
        #[test_case(1usize, "usize")]
        #[test_case(I::VERSION, "usize")]
        fn test_with_test_case<I: Ip, A: core::fmt::Debug>(_arg: A, name: &str) {
            assert!(std::any::type_name::<A>().contains(name))
        }

        fn templated_fn<I>() -> &'static str {
            std::any::type_name::<I>()
        }
        #[ip_test]
        #[test_case(std::any::type_name::<I>(), "Ipv")]
        #[test_case(Some(templated_fn::<I>()), "Ipv"; "with_name")]
        fn test_with_i_as_template_param<I: Ip>(full_name: impl core::fmt::Debug, name: &str) {
            assert!(format!("{:?}", full_name).contains(name))
        }

        #[ip_test]
        #[test_case(Some(Some(I::Addr::new())))]
        fn test_with_i_in_arg_type<I: Ip>(addr: Option<Option<I::Addr>>) {
            let _: I::Addr = addr.unwrap().unwrap();
        }

        fn produce_default_addr<I: Ip>() -> I::Addr {
            <I::Addr as IpAddress>::new()
        }

        #[ip_test]
        #[test_case(produce_default_addr::<I>)]
        fn test_with_i_in_other_type_trait_bounds<I: Ip, F: FnOnce() -> I::Addr>(f: F) {
            let _addr = f();
        }
    }

    mod fake_test_case {
        use ::fake_test_case::test_case;

        use super::*;

        /// The #[ip_test] macro can't distinguish between the real #[test_case]
        /// macro and the fake one used here. Use the fake one, which inserts
        /// the test name in the argument list, to verify that the test name
        /// is being passed along with the arguments by the #[ip_test] macro.
        #[ip_test]
        #[test_case(123, 456; "name_from_macro")]
        fn case_with_name<I: Ip>(a: u8, b: u16, name: &str) {
            assert_eq!((a, b), (123, 456));
            assert_eq!(name, "name_from_macro");
        }
    }
}
