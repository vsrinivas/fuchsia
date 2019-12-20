// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This mod defines Authority, a trait whose interface allows components to
/// to participate in different lifespans.
pub mod base;

/// This mod provides a concrete implementation of the agent authority.
pub mod authority_impl;
