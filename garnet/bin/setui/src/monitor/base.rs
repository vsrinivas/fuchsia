// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common definitions for resource monitors.
//!
//! # Summary
//!
//! This module defines the common elements surrounding resource monitors.
//! Monitors are intended to be discrete units for capturing details around
//! resource usage and reporting them back. The elements found in this module
//! are intended only for the monitors and therefore seperate from other
//! definitions, such as the monitor MessageHub, which is used by other entities
//! like agents.

use crate::internal::monitor;
use futures::future::BoxFuture;
use std::sync::Arc;

/// `Context` is passed to monitors through [`GenerateMonitor`] to provide
/// the necessary facilities to connect and listen to various aspects of the
/// setting service. It is expected that there will be new additions to
/// `Context` as the variety of monitored resources expands.
#[derive(Clone)]
pub struct Context {
    pub monitor_messenger_factory: monitor::message::Factory,
}

/// `GenerateMonitor` defines the closure for generating a monitor. This is
/// passed into the setting service's environment builder to specify a
/// participating monitor.
pub type GenerateMonitor =
    Arc<dyn Fn(Context) -> BoxFuture<'static, Result<(), anyhow::Error>> + Send + Sync>;
