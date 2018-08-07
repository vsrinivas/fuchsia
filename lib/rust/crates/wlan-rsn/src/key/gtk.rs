// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cipher::Cipher;
use crypto_utils::prf;
use {Error, Result};

#[derive(Debug, Clone, PartialEq)]
pub struct Gtk {
    gtk: Vec<u8>,
    key_id: u8,
    tk_len: usize,

    // TODO(hahnr): Add TKIP Tx/Rx MIC support (IEEE 802.11-2016, 12.8.2).
}

impl Gtk {
    pub fn from_gtk(gtk: Vec<u8>, key_id: u8) -> Gtk {
        Gtk {
            tk_len: gtk.len(),
            gtk: gtk,
            key_id,
        }
    }

    // IEEE 802.11-2016, 12.7.1.4
    pub fn new(gmk: &[u8], key_id: u8, aa: &[u8; 6], gnonce: &[u8; 32], cipher: &Cipher) -> Result<Gtk> {
        let tk_bits = cipher
            .tk_bits()
            .ok_or_else(|| Error::PtkHierarchyUnsupportedCipherError)?;

        // data length = 6 (aa) + 32 (gnonce)
        let mut data: [u8; 38] = [0; 38];
        data[0..6].copy_from_slice(&aa[..]);
        data[6..].copy_from_slice(&gnonce[..]);

        let gtk_bytes = prf(gmk, "Group key expansion", &data, tk_bits as usize)?;
        let gtk = Gtk {
            gtk: gtk_bytes,
            key_id,
            tk_len: (tk_bits / 8) as usize,
        };
        Ok(gtk)
    }

    pub fn tk(&self) -> &[u8] {
        &self.gtk[0..self.tk_len]
    }

    pub fn gtk(&self) -> &[u8] {
        &self.gtk[..]
    }

    pub fn key_id(&self) -> u8 {
        self.key_id
    }
}

// TODO(hahnr): Add tests.
