// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::server::directory::FxDirectory, std::sync::Arc};

#[derive(Clone)]
// FxNode is a strong reference to a node in the filesystem hierarchy (either a file or directory).
pub enum FxNode {
    Dir(Arc<FxDirectory>),
}
