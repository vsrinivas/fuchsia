// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate mundane;

use mundane::public::ec::*;
use mundane::public::marshal_private_key_der;

#[test]
fn parse_any_curve() {
    let p256 = marshal_private_key_der(&EcPrivKey::<P256>::generate(P256).unwrap());
    let p384 = marshal_private_key_der(&EcPrivKey::<P384>::generate(P384).unwrap());
    let p521 = marshal_private_key_der(&EcPrivKey::<P521>::generate(P521).unwrap());

    assert_eq!(
        parse_private_key_der_any_curve(&p256)
            .unwrap()
            .curve()
            .curve(),
        EllipticCurve::P256
    );
    assert_eq!(
        parse_private_key_der_any_curve(&p384)
            .unwrap()
            .curve()
            .curve(),
        EllipticCurve::P384
    );
    assert_eq!(
        parse_private_key_der_any_curve(&p521)
            .unwrap()
            .curve()
            .curve(),
        EllipticCurve::P521
    );
}
