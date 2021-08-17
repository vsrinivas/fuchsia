// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::component_tree::NodePath;

impl Serialize for NodePath {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let path = format!("/{}", self.as_vec().join("/"));
        serializer.serialize_str(&path)
    }
}
