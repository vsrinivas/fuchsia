// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::suggestion_providers::{
    contextual_suggestions_provider::*, package_suggestions_provider::*,
};

mod contextual_suggestions_provider;
mod package_suggestions_provider;
