// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use carnelian::drawing::load_font;
use carnelian::drawing::FontFace;
use lazy_static::lazy_static;
use std::path::PathBuf;

const DEFAULT_FONT_PATH: &str = "/pkg/data/fonts/Roboto-Regular.ttf";

lazy_static! {
    static ref FONT_FACE: FontFace =
        load_font(PathBuf::from(DEFAULT_FONT_PATH)).expect("failed to open  font file");
}

pub fn get_default_font_face() -> &'static FontFace {
    &FONT_FACE
}
