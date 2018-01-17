// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(unused_variables)]

pub enum Error {}

pub trait Algorithm {
    fn wrap(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error>;
    fn unwrap(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error>;
}

pub struct NistAes;

impl Algorithm for NistAes {
    fn wrap(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error> {
        // TODO(hahnr): Implement.
        unimplemented!()
    }

    fn unwrap(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error> {
        // TODO(hahnr): Implement.
        unimplemented!()
    }
}