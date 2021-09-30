// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::sync::{Arc, Weak};

use crate::errno;
use crate::error;
use crate::fs::socket::*;
use crate::types::*;

/// A registry of abstract sockets.
///
/// AF_UNIX sockets can be bound either to nodes in the file system or to
/// abstract addresses that are independent of the file system. This object
/// holds the bindings to abstract addresses.
///
/// See "abstract" in https://man7.org/linux/man-pages/man7/unix.7.html
pub struct AbstractSocketNamespace {
    table: Mutex<HashMap<Vec<u8>, Weak<Mutex<Socket>>>>,
}

impl AbstractSocketNamespace {
    pub fn new() -> Arc<AbstractSocketNamespace> {
        Arc::new(AbstractSocketNamespace { table: Mutex::new(HashMap::new()) })
    }

    pub fn bind(&self, address: Vec<u8>, socket: &SocketHandle) -> Result<(), Errno> {
        let mut table = self.table.lock();
        match table.entry(address.clone()) {
            Entry::Vacant(entry) => {
                socket.lock().bind(SocketAddress::Unix(address))?;
                entry.insert(Arc::downgrade(socket));
            }
            Entry::Occupied(mut entry) => {
                let occupant = entry.get().upgrade();
                if occupant.is_some() {
                    return error!(EADDRINUSE);
                }
                socket.lock().bind(SocketAddress::Unix(address))?;
                entry.insert(Arc::downgrade(socket));
            }
        }
        Ok(())
    }

    pub fn lookup(&self, address: &[u8]) -> Result<SocketHandle, Errno> {
        let table = self.table.lock();
        table.get(address).and_then(|weak| weak.upgrade()).ok_or_else(|| errno!(ECONNREFUSED))
    }
}
