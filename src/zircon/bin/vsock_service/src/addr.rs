// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_hardware_vsock::Addr as Raw,
    std::{
        hash::{Hash, Hasher},
        ops::{Deref, DerefMut},
    },
};

#[derive(Debug)]
pub struct Vsock {
    inner: Raw,
}

impl From<Raw> for Vsock {
    fn from(addr: Raw) -> Self {
        Vsock { inner: addr }
    }
}

impl PartialEq for Vsock {
    fn eq(&self, other: &Self) -> bool {
        self.inner.local_port == other.inner.local_port
            && self.inner.remote_port == other.inner.remote_port
            && self.inner.remote_cid == other.inner.remote_cid
    }
}

impl Eq for Vsock {}

impl Hash for Vsock {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.inner.local_port.hash(state);
        self.inner.remote_port.hash(state);
        self.inner.remote_cid.hash(state);
    }
}

impl Deref for Vsock {
    type Target = Raw;
    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl DerefMut for Vsock {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.inner
    }
}

impl Clone for Vsock {
    fn clone(&self) -> Self {
        Vsock {
            inner: Raw {
                local_port: self.inner.local_port,
                remote_port: self.inner.remote_port,
                remote_cid: self.inner.remote_cid,
            },
        }
    }
}

impl Vsock {
    pub fn new(local_port: u32, remote_port: u32, remote_cid: u32) -> Vsock {
        Vsock { inner: Raw { local_port, remote_port, remote_cid } }
    }
}
