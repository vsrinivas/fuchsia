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
        assert_eq!(0, foo.raw());
    }

    #[test]
    pub fn getters() {
        let foo = Foo(1);
        assert!(foo.a());
        assert_eq!(0, foo.b());
        assert!(!foo.c());
        assert_eq!(0, foo.d());
        assert_eq!(1, foo.raw());

        let foo = Foo(0b00_0_1001_0);
        assert!(!foo.a());
        assert_eq!(0b1001, foo.b());
        assert!(!foo.c());
        assert_eq!(0, foo.d());
        assert_eq!(0b00_0_1001_0, foo.raw());

        let foo = Foo(0b_00_1_0000_0);
        assert!(!foo.a());
        assert_eq!(0, foo.b());
        assert!(foo.c());
        assert_eq!(0, foo.d());
        assert_eq!(0b_00_1_0000_0, foo.raw());

        let foo = Foo(0b_11_0_0000_0);
        assert!(!foo.a());
        assert_eq!(0, foo.b());
        assert!(!foo.c());
        assert_eq!(0b11, foo.d());
        assert_eq!(0b_11_0_0000_0, foo.raw())
    }

    #[test]
    pub fn setters_truncate() {
        let mut foo = Foo(0);
        foo.set_b(0b11111);
        assert_eq!(0b1111, foo.b());
        assert_eq!(0b_00_0_1111_0, foo.0);
        assert_eq!(0b_00_0_1111_0, foo.raw());
    }

    #[test]
    pub fn bool_setter() {
        let mut foo = Foo(0);
        foo.set_c(true);
        assert_eq!(0b_00_1_0000_0, foo.0);
        assert_eq!(0b_00_1_0000_0, foo.raw());
        foo.set_c(false);
        assert_eq!(0b_00_0_0000_0, foo.0);
        assert_eq!(0b_00_0_0000_0, foo.raw());
    }

    #[test]
    pub fn setters_dont_touch_other_fields() {
        let mut foo = Foo(!0);
        foo.set_b(0);
        assert_eq!(0b_11_1_0000_1, foo.0);
        assert_eq!(0b_11_1_0000_1, foo.raw());
        foo.set_c(false);
        assert_eq!(0b_11_0_0000_1, foo.0);
        assert_eq!(0b_11_0_0000_1, foo.raw());
    }

    #[test]
    pub fn builders() {
        let foo = Foo(0).with_b(0b1001).with_c(true).with_d(0b10);
        assert_eq!(0b10_1_1001_0, foo.0);
        assert_eq!(0b10_1_1001_0, foo.raw());
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
        assert_eq!(0xdeadbeef0000_0000_0000_0000_0000_0000, big.raw());

        let big = Big(!0);
        assert_eq!(0xffff_ffff_ffff, big.tail());
        assert_eq!(0xffff_ffff_ffff_ffff_ffff, big.head());
        assert_eq!(0xffff_ffff_ffff_ffff_ffff_ffff_ffff_ffff, big.0);
        assert_eq!(0xffff_ffff_ffff_ffff_ffff_ffff_ffff_ffff, big.raw());
    }

    #[bitfield(
        0..=3    head,
        4..=20   middle,
        21       bit,
        22..=23  tail,
    )]
    pub struct Array([u8; 3]);

    #[test]
    pub fn array() {
        let array = Array([0b0000_0000, 0b10101111, 0b01_1_00010]);
        assert_eq!(array.head(), 0b0000);
        assert_eq!(array.middle(), 0b00010_10101111_0000);
        assert!(array.bit());
        assert_eq!(array.tail(), 0b01);

        let mut array2 = Array([0u8; 3]);
        array2.set_head(array.head());
        array2.set_middle(array.middle());
        array2.set_bit(array.bit());
        array2.set_tail(array.tail());
        assert_eq!(&array.0[..], &array2.0[..]);
    }

    #[test]
    pub fn overwrite_array() {
        let mut array = Array([0b0011_1111, 0b11100000, 0b11_1_11100]);
        array.set_head(0b1010);
        array.set_middle(0b00011_11111111_0101);
        array.set_bit(false);
        array.set_tail(0b00);
        assert_eq!(&array.0[..], &[0b0101_1010, 0b11111111, 0b00_0_00011][..])
    }

    #[derive(Debug)]
    struct CustomType(pub u8);

    #[bitfield(
        0..=5   a as CustomType(u8),
        6..=15  b,
    )]
    struct WithCustomType(pub u16);

    #[test]
    pub fn custom_type() {
        let mut x = WithCustomType(0).with_a(CustomType(0xf));
        let a: CustomType = x.a();
        assert_eq!(0xf, a.0);
        assert_eq!(0x000f, x.0);
        assert_eq!(0x000f, x.raw());
        x.set_a(CustomType(12));
        assert_eq!(12, x.a().0);
    }

    #[derive(Debug)]
    struct CustomBoolType(pub bool);

    #[bitfield(
        0       a as CustomBoolType(bool),
        1..=15  b,
    )]
    struct WithCustomBoolType(pub u16);

    #[test]
    pub fn custom_bool_type() {
        let mut x = WithCustomBoolType(0).with_a(CustomBoolType(true));
        let a: CustomBoolType = x.a();
        assert!(a.0);
        assert_eq!(0x0001, x.0);
        assert_eq!(0x0001, x.raw());
        x.set_a(CustomBoolType(false));
        assert!(!x.a().0);
    }

    #[bitfield(
        0..=3   alpha,
        4..=11  beta as CustomType(u8),
        12..=13 _,
        14      gamma as CustomBoolType(bool),
        15      delta,
    )]
    struct ForDebug(pub u16);

    #[test]
    pub fn debug() {
        let string = format!("{:?}", ForDebug(0x0fff));
        assert_eq!(
            "ForDebug { 0: 0x0fff, alpha: 0xf, beta: CustomType(255), \
             gamma: CustomBoolType(false), delta: false }",
            string
        );
    }

    #[test]
    pub fn debug_pretty() {
        let string = format!("{:#?}", ForDebug(0x0fff));
        assert_eq!(
            "ForDebug {\
             \n    0: 0x0fff,\
             \n    alpha: 0xf,\
             \n    beta: CustomType(255),\
             \n    gamma: CustomBoolType(false),\
             \n    delta: false,\
             \n}",
            string
        );
    }

    #[bitfield(
        0..=3   head,
        4..=7   union {
                    foo,
                    bar as CustomType(u8),
                },
        8..=15  tail
    )]
    struct Aliased(pub u16);

    #[test]
    pub fn aliased() {
        let mut a = Aliased(0).with_head(5).with_tail(0x77);
        assert_eq!(5, a.head());
        assert_eq!(0, a.foo());
        assert_eq!(0, a.bar().0);
        assert_eq!(0x77, a.tail());

        a.set_foo(0xf);
        assert_eq!(0x77f5, a.0);
        assert_eq!(0x77f5, a.raw());
        assert_eq!(5, a.head());
        assert_eq!(0xf, a.foo());
        assert_eq!(0xf, a.bar().0);
        assert_eq!(0x77, a.tail());

        a.set_bar(CustomType(1));
        assert_eq!(0x7715, a.0);
        assert_eq!(0x7715, a.raw());
        assert_eq!(5, a.head());
        assert_eq!(1, a.foo());
        assert_eq!(1, a.bar().0);
        assert_eq!(0x77, a.tail());
    }
}
