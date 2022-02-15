// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Trait providing a method for converting something into an escaped ASCII string.
pub trait ToEscapedString {
    /// Escapes the receiver into an ASCII string.
    fn to_escaped_string(&self) -> String;
}

impl ToEscapedString for [u8] {
    fn to_escaped_string(&self) -> String {
        self.iter()
            .copied()
            .flat_map(std::ascii::escape_default)
            .map(char::from)
            .collect::<String>()
    }
}
