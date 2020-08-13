// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This mod defines Switchboard, a trait whose interface allows components to
/// make requests upon settings. It also describes the types used to communicate
/// with the Switchboard.
pub mod base;

/// This mod provides a concrete implementation of the Switchboard.
pub mod switchboard;

/// This mod provides a standard way to handle a hanging get in a mod
pub mod hanging_get_handler;

/// This mod provides the service's internal representations of accessibility info.
pub mod accessibility_types;

/// This mod provides the service's internal representations of light info.
pub mod light_types;

/// This mod provides the service's internal representations of internationalization info.
pub mod intl_types;
