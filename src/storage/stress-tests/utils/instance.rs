// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Instance state can be shared with all actors via this trait.
///
/// Environment::new_instance() produces an InstanceUnderTest object.
/// This object is cloned and passed into Actor::perform().
pub trait InstanceUnderTest: 'static + Send + Sync + Clone {}
