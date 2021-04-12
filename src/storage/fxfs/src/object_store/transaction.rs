// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::object_store::{allocator::AllocatorItem, record::ObjectItem},
    serde::{Deserialize, Serialize},
    std::vec::Vec,
};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum Mutation {
    // Inserts a record.
    Insert { item: ObjectItem },
    // Inserts or replaces a record.
    ReplaceOrInsert { item: ObjectItem },
    // Merges a record.
    Merge { item: ObjectItem },
    Allocate(AllocatorItem),
    Deallocate(AllocatorItem),
    // Seal the mutable layer and create a new one.
    TreeSeal,
    // Discards all non-mutable layers.
    TreeCompact,
}

/// A transaction groups mutation records to be commited as a group.
pub struct Transaction {
    pub mutations: Vec<(u64, Mutation)>,
}

impl Transaction {
    pub fn new() -> Transaction {
        Transaction { mutations: Vec::new() }
    }

    pub fn add(&mut self, object_id: u64, mutation: Mutation) {
        assert!(object_id != 0);
        self.mutations.push((object_id, mutation));
    }

    pub fn is_empty(&self) -> bool {
        self.mutations.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use crate::object_store::transaction::{Mutation, Transaction};

    #[test]
    fn test_simple() {
        let mut t = Transaction::new();
        t.add(1, Mutation::TreeSeal);
        assert!(!t.is_empty());
    }
}
