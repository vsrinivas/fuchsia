// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use carnelian::drawing::load_font;
use carnelian::drawing::FontFace;
use std::path::PathBuf;

const DEFAULT_FONT_PATH: &str = "/pkg/data/fonts/Roboto-Regular.ttf";

pub fn load_default_font_face() -> Result<FontFace, Error> {
    load_font(PathBuf::from(DEFAULT_FONT_PATH))
}
