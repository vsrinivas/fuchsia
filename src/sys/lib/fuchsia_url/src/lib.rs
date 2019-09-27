// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod boot_url;
pub mod errors;
mod parse;
pub mod pkg_url;

use percent_encoding::{AsciiSet, CONTROLS};

/// https://url.spec.whatwg.org/#fragment-percent-encode-set
pub(crate) const FRAGMENT: &AsciiSet = &CONTROLS.add(b' ').add(b'"').add(b'<').add(b'>').add(b'`');
