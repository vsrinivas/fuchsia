// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The base mod defines the basic communication building blocks used by setting
/// handlers.
pub mod base;

/// This mod implements a struct for managing storage of the setting_proxies inspect node.
pub mod setting_proxy_inspect_info;

/// Trait definition for setting controllers and a wrapper handler to interface
/// with the proxy.
pub mod setting_handler;

pub(crate) mod setting_proxy;

/// This mod implements a factory that can be populated to provide handlers on
/// demand.
pub(crate) mod setting_handler_factory_impl;
