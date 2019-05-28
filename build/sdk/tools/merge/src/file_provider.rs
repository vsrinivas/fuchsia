// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use sdk_metadata::TargetArchitecture;

/// A trait to extract files references from SDK elements.
pub trait FileProvider {
    /// Returns the list of architecture-independent files for this element.
    fn get_common_files(&self) -> Vec<String> {
        Vec::new()
    }

    /// Returns the list of architecture-dependent files for this element.
    fn get_arch_files(&self) -> HashMap<TargetArchitecture, Vec<String>> {
        HashMap::new()
    }

    /// Returns all the files associated with this element.
    fn get_all_files(&self) -> Vec<String> {
        let mut result = self.get_common_files();
        for (_, files) in self.get_arch_files() {
            result.extend(files);
        }
        result
    }
}
