// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::logs::{buffer::ArcList, message::Message};
use parking_lot::Mutex;
use std::sync::Arc;

#[derive(Clone)]
pub struct BudgetManager {
    state: Arc<Mutex<BudgetState>>,
}

impl BudgetManager {
    pub fn new(capacity: usize, buffer: &ArcList<Message>) -> Self {
        Self {
            state: Arc::new(Mutex::new(BudgetState {
                buffer: buffer.clone(),
                capacity,
                current: 0,
            })),
        }
    }

    pub fn handle(&self) -> BudgetHandle {
        BudgetHandle { state: self.state.clone() }
    }
}

struct BudgetState {
    buffer: ArcList<Message>,
    current: usize,
    capacity: usize,
}

impl BudgetState {
    fn allocate(&mut self, size: usize) {
        self.current += size;
        while self.current > self.capacity {
            self.current -= self
                .buffer
                .pop_front()
                .expect("if we need to reduce capacity, there are messages")
                .metadata
                .size_bytes;
        }
    }
}

pub struct BudgetHandle {
    state: Arc<Mutex<BudgetState>>,
}

impl BudgetHandle {
    pub fn allocate(&self, size: usize) {
        self.state.lock().allocate(size);
    }
}
