// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::sync::{Arc, Weak};

use crate::fs::socket::*;
use crate::lock::Mutex;
use crate::types::*;

/// A registry of abstract sockets.
///
/// AF_UNIX sockets can be bound either to nodes in the file system or to
/// abstract addresses that are independent of the file system. This object
/// holds the bindings to abstract addresses.
///
/// See "abstract" in https://man7.org/linux/man-pages/man7/unix.7.html
pub struct AbstractSocketNamespace<K> {
    table: Mutex<HashMap<K, Weak<Socket>>>,
    address_maker: Box<dyn Fn(K) -> SocketAddress + Send + Sync>,
}

pub type AbstractUnixSocketNamespace = AbstractSocketNamespace<Vec<u8>>;
pub type AbstractVsockSocketNamespace = AbstractSocketNamespace<u32>;

impl<K> AbstractSocketNamespace<K>
where
    K: std::cmp::Eq + std::hash::Hash + Clone,
{
    pub fn new(
        address_maker: Box<dyn Fn(K) -> SocketAddress + Send + Sync>,
    ) -> Arc<AbstractSocketNamespace<K>> {
        Arc::new(AbstractSocketNamespace::<K> { table: Mutex::new(HashMap::new()), address_maker })
    }

    pub fn bind(&self, address: K, socket: &SocketHandle) -> Result<(), Errno> {
        let mut table = self.table.lock();
        match table.entry(address.clone()) {
            Entry::Vacant(entry) => {
                socket.bind((self.address_maker)(address))?;
                entry.insert(Arc::downgrade(socket));
            }
            Entry::Occupied(mut entry) => {
                let occupant = entry.get().upgrade();
                if occupant.is_some() {
                    return error!(EADDRINUSE);
                }
                socket.bind((self.address_maker)(address))?;
                entry.insert(Arc::downgrade(socket));
            }
        }
        Ok(())
    }

    pub fn lookup(&self, address: &K) -> Result<SocketHandle, Errno> {
        let table = self.table.lock();
        table.get(address).and_then(|weak| weak.upgrade()).ok_or_else(|| errno!(ECONNREFUSED))
    }
}
