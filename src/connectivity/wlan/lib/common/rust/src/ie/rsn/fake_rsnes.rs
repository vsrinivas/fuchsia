// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ie::rsn::{
    akm::{Akm, PSK},
    cipher::{Cipher, CCMP_128, TKIP},
    rsne::Rsne,
    suite_selector::OUI,
};

pub fn fake_wpa2_a_rsne() -> Rsne {
    let mut rsne = Rsne::new();
    rsne.group_data_cipher_suite = Some(Cipher { oui: OUI, suite_type: CCMP_128 });
    rsne.pairwise_cipher_suites.push(Cipher { oui: OUI, suite_type: CCMP_128 });
    rsne.pairwise_cipher_suites.push(Cipher { oui: OUI, suite_type: TKIP });
    rsne.akm_suites.push(Akm { oui: OUI, suite_type: PSK });
    rsne
}

pub fn fake_wpa2_s_rsne() -> Rsne {
    let mut rsne = Rsne::new();
    rsne.group_data_cipher_suite = Some(Cipher { oui: OUI, suite_type: CCMP_128 });
    rsne.pairwise_cipher_suites.push(Cipher { oui: OUI, suite_type: CCMP_128 });
    rsne.akm_suites.push(Akm { oui: OUI, suite_type: PSK });
    rsne
}
