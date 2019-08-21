// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde_derive::Serialize;

#[derive(Serialize, Debug)]
pub struct Results {
    messages: Vec<String>,
    failed: bool,
}

impl Results {
    pub fn new() -> Results {
        Results { messages: Vec::new(), failed: false }
    }

    pub fn error(&mut self, message: String) {
        self.messages.push(message);
        self.failed = true;
    }

    pub fn to_json(&self) -> String {
        match serde_json::to_string(self) {
            Ok(string) => string,
            Err(e) => format!("{{error: \"Converting to json: {:?}\"}}", e),
        }
    }

    pub fn failed(&self) -> bool {
        self.failed
    }
}
