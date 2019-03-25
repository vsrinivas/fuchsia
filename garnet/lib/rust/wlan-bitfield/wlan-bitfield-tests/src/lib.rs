// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    use wlan_bitfield::bitfield;

    #[bitfield(
        0       a,
        1..=4   b,
        5       c,
        6..=7   d,
    )]
    pub struct Foo(u8);

    #[test]
    pub fn getters_zero() {
        let foo = Foo(0);
        assert!(!foo.a());
        assert_eq!(0, foo.b());
        assert!(!foo.c());
        assert_eq!(0, foo.d());
    }

    #[test]
    pub fn getters() {
        let foo = Foo(1);
        assert!(foo.a());
        assert_eq!(0, foo.b());
        assert!(!foo.c());
        assert_eq!(0, foo.d());

        let foo = Foo(0b00_0_1001_0);
        assert!(!foo.a());
        assert_eq!(0b1001, foo.b());
        assert!(!foo.c());
        assert_eq!(0, foo.d());

        let foo = Foo(0b_00_1_0000_0);
        assert!(!foo.a());
        assert_eq!(0, foo.b());
        assert!(foo.c());
        assert_eq!(0, foo.d());

        let foo = Foo(0b_11_0_0000_0);
        assert!(!foo.a());
        assert_eq!(0, foo.b());
        assert!(!foo.c());
        assert_eq!(0b11, foo.d());
    }

    #[test]
    pub fn setters_truncate() {
        let mut foo = Foo(0);
        foo.set_b(0b11111);
        assert_eq!(0b1111, foo.b());
        assert_eq!(0b_00_0_1111_0, foo.0)
    }

    #[test]
    pub fn bool_setter() {
        let mut foo = Foo(0);
        foo.set_c(true);
        assert_eq!(0b_00_1_0000_0, foo.0);
        foo.set_c(false);
        assert_eq!(0b_00_0_0000_0, foo.0);
    }

    #[test]
    pub fn setters_dont_touch_other_fields() {
        let mut foo = Foo(!0);
        foo.set_b(0);
        assert_eq!(0b_11_1_0000_1, foo.0);
        foo.set_c(false);
        assert_eq!(0b_11_0_0000_1, foo.0);
    }

    #[test]
    pub fn builders() {
        let foo = Foo(0).with_b(0b1001).with_c(true).with_d(0b10);
        assert_eq!(0b10_1_1001_0, foo.0);
    }

    #[bitfield(
        0..=79      head,
        80..=127    tail,
    )]
    pub struct Big(u128);

    #[test]
    pub fn u128() {
        let mut big = Big(0);
        big.set_tail(0xdeadbeef0000);
        assert_eq!(0xdeadbeef0000, big.tail());
        assert_eq!(0xdeadbeef0000_0000_0000_0000_0000_0000, big.0);

        let big = Big(!0);
        assert_eq!(0xffff_ffff_ffff, big.tail());
        assert_eq!(0xffff_ffff_ffff_ffff_ffff, big.head());
    }
}
