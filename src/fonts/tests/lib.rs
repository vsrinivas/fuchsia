// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub const FONTS_CMX: &str = "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx";

pub const MANIFEST_ALIASES: &str = "/testdata/aliases.font_manifest.json";
pub const MANIFEST_EPHEMERAL: &str = "/testdata/ephemeral.font_manifest.json";
pub const MANIFEST_TEST_FONTS_SMALL: &str = "/config/data/test_fonts_small.font_manifest.json";
pub const MANIFEST_TEST_FONTS_MEDIUM: &str = "/config/data/test_fonts_medium.font_manifest.json";
pub const MANIFEST_TEST_FONTS_LARGE: &str = "/config/data/test_fonts_large.font_manifest.json";

mod experimental_api;
mod old_api;
mod reviewed_api;
