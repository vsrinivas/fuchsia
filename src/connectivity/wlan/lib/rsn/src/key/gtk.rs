// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Remove once GtkProvider is used.
#[allow(unused)]
use crate::crypto_utils::prf;
use crate::{rsn_ensure, Error};
use mundane::bytes;
use std::hash::{Hash, Hasher};
use wlan_common::ie::rsn::cipher::Cipher;

/// This GTK provider does not support key rotations yet.
#[derive(Debug)]
pub struct GtkProvider {
    key: Box<[u8]>,
    cipher: Cipher,
}

fn generate_random_gtk(len: usize) -> Box<[u8]> {
    let mut key = vec![0; len];
    bytes::rand(&mut key[..]);
    key.into_boxed_slice()
}

impl GtkProvider {
    pub fn new(cipher: Cipher) -> Result<GtkProvider, anyhow::Error> {
        let tk_bytes = cipher.tk_bytes().ok_or(Error::GtkHierarchyUnsupportedCipherError)?;
        Ok(GtkProvider { cipher, key: generate_random_gtk(tk_bytes) })
    }

    pub fn get_gtk(&self) -> Result<Gtk, Error> {
        Gtk::from_gtk(self.key.to_vec(), 0, self.cipher.clone(), 0)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Gtk {
    pub gtk: Vec<u8>,
    key_id: u8,
    tk_len: usize,
    pub rsc: u64,
    pub cipher: Cipher,
    // TODO(hahnr): Add TKIP Tx/Rx MIC support (IEEE 802.11-2016, 12.8.2).
}

/// Custom Hash implementation which doesn't take the RSC or cipher suite into consideration.
impl Hash for Gtk {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.key_id.hash(state);
        self.tk().hash(state);
    }
}

impl Gtk {
    pub fn from_gtk(gtk: Vec<u8>, key_id: u8, cipher: Cipher, rsc: u64) -> Result<Gtk, Error> {
        let tk_bits = cipher.tk_bits().ok_or(Error::GtkHierarchyUnsupportedCipherError)?;
        let tk_len = (tk_bits / 8) as usize;
        rsn_ensure!(gtk.len() >= tk_len, "GTK must be larger than the resulting TK");

        Ok(Gtk { tk_len, gtk, key_id, cipher, rsc })
    }

    // IEEE 802.11-2016, 12.7.1.4
    pub fn new(
        gmk: &[u8],
        key_id: u8,
        aa: &[u8; 6],
        gnonce: &[u8; 32],
        cipher: Cipher,
        rsc: u64,
    ) -> Result<Gtk, anyhow::Error> {
        let tk_bits = cipher.tk_bits().ok_or(Error::GtkHierarchyUnsupportedCipherError)?;

        // data length = 6 (aa) + 32 (gnonce)
        let mut data: [u8; 38] = [0; 38];
        data[0..6].copy_from_slice(&aa[..]);
        data[6..].copy_from_slice(&gnonce[..]);

        let gtk_bytes = prf(gmk, "Group key expansion", &data, tk_bits as usize)?;
        Ok(Gtk { gtk: gtk_bytes, key_id, tk_len: (tk_bits / 8) as usize, cipher, rsc })
    }

    pub fn tk(&self) -> &[u8] {
        &self.gtk[0..self.tk_len]
    }

    pub fn key_id(&self) -> u8 {
        self.key_id
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;
    use wlan_common::ie::rsn::{cipher, suite_selector::OUI};

    #[test]
    fn test_gtk_generation() {
        let mut gtks = HashSet::new();
        for _ in 0..10000 {
            let provider = GtkProvider::new(Cipher { oui: OUI, suite_type: cipher::CCMP_128 })
                .expect("failed creating GTK Provider");
            let gtk = provider.get_gtk().expect("could not read GTK").tk().to_vec();
            assert!(gtk.iter().any(|&x| x != 0));
            assert!(!gtks.contains(&gtk));
            gtks.insert(gtk);
        }
    }
}
