// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use regex::Captures;

pub fn capture_name<'a>(capture: &Captures<'a>, name: &str) -> Result<String, &'static str> {
    Ok(capture.name(name).ok_or("Regex capture error")?.as_str().trim().to_string())
}
