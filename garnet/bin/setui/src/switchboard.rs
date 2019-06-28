// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This mod defines Switchboard, a trait whose interface allows components to
/// make requests upon settings. It also describes the types used to communicate
/// with the Switchboard.
pub mod base;

/// This mode provides a concrete implementation of the Switchboard.
pub mod switchboard_impl;
