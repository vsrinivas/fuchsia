// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Copyright 2016 Joe Wilm, The Alacritty Project Contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

pub struct Clipboard {}

impl Clipboard {
    pub fn new() -> Self {
        Self {}
    }

    // Use for tests and ref-tests
    pub fn new_nop() -> Self {
        Self {}
    }
}

impl Default for Clipboard {
    fn default() -> Self {
        Self {}
    }
}

#[derive(Debug)]
pub enum ClipboardType {
    Clipboard,
    Selection,
}

// Fuchsia does not currently support clipboards but the Clipboard struct
// is required for the terminal. For now, we will leave the struct empty until
// we support this functionality.
impl Clipboard {
    pub fn store(&mut self, _ty: ClipboardType, _text: impl Into<String>) {
        // Intentionally blank
    }

    pub fn load(&mut self, _ty: ClipboardType) -> String {
        String::new()
    }
}
