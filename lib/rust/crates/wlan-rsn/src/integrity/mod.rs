// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(unused_variables)]

pub enum Error {}

pub trait Algorithm {
    fn verify(&self, key: &[u8], data: &[u8]) -> bool;
    fn compute(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error>;
}

pub struct HmacSha1128;

impl Algorithm for HmacSha1128 {
    fn verify(&self, key: &[u8], data: &[u8]) -> bool {
        // TODO(hahnr): Implement.
        unimplemented!()
    }

    fn compute(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error> {
        // TODO(hahnr): Implement.
        unimplemented!()
    }
}

// TODO(hahnr): Add other required integrity algorithms.
// IEEE 802.11-2016, 12.7.3, Table 12-8