// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        device::Device,
        object_store::{
            allocator::Allocator,
            filesystem::{Filesystem, ObjectManager, ObjectSync},
            journal::JournalCheckpoint,
            transaction::Transaction,
            ObjectStore,
        },
    },
    async_trait::async_trait,
    std::sync::Arc,
};

pub struct FakeFilesystem {
    device: Arc<dyn Device>,
    object_manager: Arc<ObjectManager>,
}

impl FakeFilesystem {
    pub fn new(device: Arc<dyn Device>) -> Arc<Self> {
        let object_manager = Arc::new(ObjectManager::new());
        Arc::new(FakeFilesystem { device, object_manager })
    }

    pub fn object_manager(&self) -> &Arc<ObjectManager> {
        &self.object_manager
    }
}

#[async_trait]
impl Filesystem for FakeFilesystem {
    async fn commit_transaction(&self, transaction: Transaction) {
        for (object_id, mutation) in transaction.mutations {
            self.object_manager
                .apply_mutation(object_id, mutation, false, &JournalCheckpoint::default())
                .await;
        }
    }

    fn register_store(&self, store: &Arc<ObjectStore>) {
        self.object_manager.register_store(store);
    }

    fn begin_object_sync(&self, object_id: u64) -> ObjectSync {
        self.object_manager.begin_object_sync(object_id)
    }

    fn device(&self) -> Arc<dyn Device> {
        self.device.clone()
    }

    fn root_store(&self) -> Arc<ObjectStore> {
        self.object_manager.root_store()
    }

    fn allocator(&self) -> Arc<dyn Allocator> {
        self.object_manager.allocator()
    }
}
