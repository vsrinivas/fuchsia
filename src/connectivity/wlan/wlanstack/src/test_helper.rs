// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::inspect, fuchsia_inspect::Inspector, futures::channel::mpsc, std::sync::Arc};

// arbitrary value
const DEFAULT_BUFFER_SIZE: usize = 100;

pub fn fake_inspect_tree() -> (Arc<inspect::WlanstackTree>, mpsc::Receiver<String>) {
    let (sender, receiver) = mpsc::channel(DEFAULT_BUFFER_SIZE);
    let inspector = Inspector::new();
    (Arc::new(inspect::WlanstackTree::new(inspector, sender)), receiver)
}

pub fn create_inspect_persistence_channel() -> (mpsc::Sender<String>, mpsc::Receiver<String>) {
    const DEFAULT_BUFFER_SIZE: usize = 100; // arbitrary value
    mpsc::channel(DEFAULT_BUFFER_SIZE)
}
