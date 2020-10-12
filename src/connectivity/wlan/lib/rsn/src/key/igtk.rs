// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{key::Tk, key_data::kde},
    wlan_common::ie::rsn::cipher::Cipher,
};

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct Igtk {
    pub igtk: Vec<u8>,
    pub key_id: u16,
    pub ipn: [u8; 6],
    pub cipher: Cipher,
}

impl Igtk {
    pub fn from_kde(element: kde::Igtk, cipher: Cipher) -> Self {
        Self { igtk: element.igtk, key_id: element.id, ipn: element.ipn, cipher }
    }
}

impl Tk for Igtk {
    fn tk(&self) -> &[u8] {
        &self.igtk[..]
    }
}
