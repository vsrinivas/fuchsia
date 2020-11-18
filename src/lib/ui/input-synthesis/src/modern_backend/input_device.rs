// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Implements the `synthesizer::InputDevice` trait, and the server side of the
/// `fuchsia.input.report.InputDevice` FIDL protocol. Used by
/// `modern_backend::InputDeviceRegistry`.
#[allow(dead_code)] // TODO(fxbug.dev/63985) remove `allow`
pub(super) struct InputDevice;
