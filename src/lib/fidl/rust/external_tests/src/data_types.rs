// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fidl_rust_test_external::{FlexibleButtons, StrictButtons};

#[test]
fn strict_bits() {
    assert_eq!(StrictButtons::from_bits(0b001), Some(StrictButtons::Play));
    assert_eq!(StrictButtons::from_bits(0b1100), None);
    assert_eq!(StrictButtons::from_bits_truncate(0b1010), StrictButtons::Pause);
    assert_eq!(StrictButtons::from_bits_truncate(u32::MAX), StrictButtons::all());
    assert_eq!(StrictButtons::Stop.bits(), 0b100);

    // You can use the flexible methods on strict types, but it produces a
    // deprecation warning.
    #[allow(deprecated)]
    let has_unknown_bits = StrictButtons::Play.has_unknown_bits();
    assert_eq!(has_unknown_bits, false);
    #[allow(deprecated)]
    let get_unknown_bits = StrictButtons::Play.get_unknown_bits();
    assert_eq!(get_unknown_bits, 0);
}

#[test]
fn flexible_bits() {
    assert_eq!(FlexibleButtons::from_bits(0b001), Some(FlexibleButtons::Play));
    assert_eq!(FlexibleButtons::from_bits(0b1100), None);
    assert_eq!(
        FlexibleButtons::from_bits_allow_unknown(0b1010) & FlexibleButtons::all(),
        FlexibleButtons::Pause
    );
    assert_eq!(FlexibleButtons::Stop.bits(), 0b100);
    assert_eq!(FlexibleButtons::from_bits_allow_unknown(0b1010).bits(), 0b1010);
    assert_eq!(FlexibleButtons::from_bits_allow_unknown(u32::MAX).bits(), u32::MAX);

    assert_eq!(FlexibleButtons::Play.has_unknown_bits(), false);
    assert_eq!(FlexibleButtons::Play.get_unknown_bits(), 0);
    assert_eq!(FlexibleButtons::from_bits_allow_unknown(0b1010).has_unknown_bits(), true);
    assert_eq!(FlexibleButtons::from_bits_allow_unknown(0b1010).get_unknown_bits(), 0b1000);
    assert_eq!(FlexibleButtons::from_bits_allow_unknown(u32::MAX).has_unknown_bits(), true);
    assert_eq!(
        FlexibleButtons::from_bits_allow_unknown(u32::MAX).get_unknown_bits(),
        u32::MAX & !0b111
    );

    // Negation ANDs with the mask.
    assert_ne!(
        FlexibleButtons::from_bits_allow_unknown(0b101000101),
        FlexibleButtons::Play | FlexibleButtons::Stop
    );
    assert_eq!(!FlexibleButtons::from_bits_allow_unknown(0b101000101), FlexibleButtons::Pause);
    assert_eq!(
        !!FlexibleButtons::from_bits_allow_unknown(0b101000101),
        FlexibleButtons::Play | FlexibleButtons::Stop
    );
}
