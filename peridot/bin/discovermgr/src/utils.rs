// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Converts a module_path into a string.
/// Example: ["abc", "1:2"] -> "abc:1\:2"
pub fn encoded_module_path(module_path: Vec<String>) -> String {
    module_path.iter().map(|part| part.replace(":", "\\:")).collect::<Vec<String>>().join("-")
}
