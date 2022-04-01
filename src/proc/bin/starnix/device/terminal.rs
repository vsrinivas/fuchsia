// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::{Mutex, RwLock};
use std::collections::{BTreeSet, HashMap};
use std::sync::{Arc, Weak};

use crate::fs::devpts::*;
use crate::fs::*;
use crate::types::*;

/// Global state of the devpts filesystem.
pub struct TTYState {
    /// The terminal objects indexed by their identifier.
    pub terminals: RwLock<HashMap<u32, Weak<Terminal>>>,

    /// The devpts filesystem.
    fs: FileSystemHandle,

    /// The set of available terminal identifier.
    pts_ids_set: Mutex<PtsIdsSet>,
}

impl TTYState {
    pub fn new(fs: FileSystemHandle) -> Self {
        Self {
            terminals: RwLock::new(HashMap::new()),
            fs,
            pts_ids_set: Mutex::new(PtsIdsSet::new(DEVPTS_COUNT)),
        }
    }

    /// Returns the next available terminal.
    pub fn get_next_terminal(self: &Arc<Self>) -> Result<Arc<Terminal>, Errno> {
        let id = self.pts_ids_set.lock().get()?;
        let terminal = Arc::new(Terminal::new(self.clone(), id));
        create_pts_node(&self.fs, id)?;
        self.terminals.write().insert(id, Arc::downgrade(&terminal));
        Ok(terminal)
    }

    /// Release the terminal identifier into the set of available identifier.
    pub fn release_terminal(&self, id: u32) -> Result<(), Errno> {
        self.pts_ids_set.lock().release(id);
        self.terminals.write().remove(&id);
        Ok(())
    }
}

/// State of a given terminal. This object handles both the main and the replica terminal.
pub struct Terminal {
    /// The global devpts state.
    state: Arc<TTYState>,

    /// The identifier of the terminal.
    pub id: u32,

    /// |true| is the terminal is locked.
    pub locked: RwLock<bool>,
}

impl Terminal {
    pub fn new(state: Arc<TTYState>, id: u32) -> Self {
        Self { state, id, locked: RwLock::new(true) }
    }
}

impl Drop for Terminal {
    fn drop(&mut self) {
        self.state.release_terminal(self.id).unwrap()
    }
}

struct PtsIdsSet {
    pts_count: u32,
    next_id: u32,
    reclaimed_ids: BTreeSet<u32>,
}

impl PtsIdsSet {
    pub fn new(pts_count: u32) -> Self {
        Self { pts_count, next_id: 0, reclaimed_ids: BTreeSet::new() }
    }

    pub fn release(&mut self, id: u32) {
        assert!(self.reclaimed_ids.insert(id))
    }

    pub fn get(&mut self) -> Result<u32, Errno> {
        match self.reclaimed_ids.iter().next() {
            Some(e) => {
                let value = e.clone();
                self.reclaimed_ids.remove(&value);
                Ok(value)
            }
            None => {
                if self.next_id < self.pts_count {
                    let id = self.next_id;
                    self.next_id += 1;
                    Ok(id)
                } else {
                    error!(ENOSPC)
                }
            }
        }
    }
}
