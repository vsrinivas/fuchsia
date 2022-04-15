// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{collections::HashSet, ops::Range};

// https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml
const EPHEMERAL_PORT_RANGE: Range<u32> = 49152..65535;

pub fn is_ephemeral(port: u32) -> bool {
    port >= EPHEMERAL_PORT_RANGE.start && port < EPHEMERAL_PORT_RANGE.end
}
pub struct Tracker {
    // TODO: use a bit-vec instead
    used: HashSet<u32>,
    // Track the next port we should attempt to begin allocating from as a
    // heuristic.
    next_allocation: u32,
}

impl Tracker {
    pub fn allocate(&mut self) -> Option<u32> {
        // Search for a port starting from `next_allocation` and wrapping around if none is
        // found to try all the ports up until one before next_allocation.
        (self.next_allocation..EPHEMERAL_PORT_RANGE.end)
            .chain(EPHEMERAL_PORT_RANGE.start..self.next_allocation)
            .find(|&p| self.used.insert(p))
            .map(|x| {
                self.next_allocation = x + 1;
                x
            })
    }
    pub fn free(&mut self, port: u32) {
        if !self.used.remove(&port) {
            panic!("Tried to free unallocated port.");
        }
        self.next_allocation = std::cmp::min(self.next_allocation, port);
    }
    pub fn new() -> Self {
        Tracker { used: HashSet::new(), next_allocation: EPHEMERAL_PORT_RANGE.start }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ports_reused() {
        let mut t = Tracker::new();
        let p1 = t.allocate().unwrap();
        let p2 = t.allocate().unwrap();
        let p3 = t.allocate().unwrap();
        t.free(p2);
        let p4 = t.allocate().unwrap();
        assert_eq!(p2, p4);
        let p5 = t.allocate().unwrap();
        t.free(p4);
        t.free(p3);
        let p6 = t.allocate().unwrap();
        let p7 = t.allocate().unwrap();
        assert_eq!(p4, p6);
        assert_eq!(p3, p7);
        t.free(p1);
        t.free(p5);
        t.free(p6);
        t.free(p7);
    }
}
