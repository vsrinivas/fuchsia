// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use lazy_static::lazy_static;
use std::sync::atomic::{AtomicUsize, Ordering};

lazy_static! {
  // Suffix used for unique names.
  static ref UNIQUE_NAME_SUFFIX: AtomicUsize = AtomicUsize::new(0);
}

/// Generates a unique name that can be used in inspect nodes and properties that will be prefixed
/// by the given `prefix`.
pub fn unique_name(prefix: &str) -> String {
    let suffix = UNIQUE_NAME_SUFFIX.fetch_add(1, Ordering::Relaxed);
    format!("{}{}", prefix, suffix)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::writer::Inspector;
    use diagnostics_hierarchy::assert_data_tree;

    #[test]
    fn test_unique_name() {
        let inspector = Inspector::new();

        let name_1 = unique_name("a");
        assert_eq!(name_1, "a0");
        inspector.root().record_uint(name_1, 1);

        let name_2 = unique_name("a");
        assert_eq!(name_2, "a1");
        inspector.root().record_uint(name_2, 1);

        assert_data_tree!(inspector, root: {
            a0: 1u64,
            a1: 1u64,
        });
    }
}
