// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {std::any::Any, std::sync::Arc};

// FxNode is a node in the filesystem hierarchy (either a file or directory).
pub trait FxNode: Any + Send + Sync + 'static {
    fn object_id(&self) -> u64;
    fn into_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync + 'static>;
}
