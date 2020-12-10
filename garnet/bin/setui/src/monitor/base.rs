// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Base definitions for working with monitors and outlets.

use std::borrow::Cow;

#[derive(thiserror::Error, Debug, Clone, PartialEq)]
pub enum Error {
    #[error("MessageSetupFailure:{0:?}")]
    MessageSetupFailure(Cow<'static, str>),
    #[error("MonitorSetupFailure:{0:?}")]
    MonitorSetupFailure(Cow<'static, str>),
}

/// The monitor module defines the common elements surrounding resource
/// monitors. Monitors are intended to be discrete units for capturing details
/// around resource usage and reporting them back. The elements found in this
/// module are intended only for the monitors and therefore seperate from other
/// definitions, such as the monitor MessageHub, which is used by other entities
/// like agents.
pub mod monitor {
    use crate::internal::monitor;
    use anyhow::Error;
    use futures::future::BoxFuture;
    use std::sync::Arc;

    /// `Context` is passed to monitors through [`Generate`] to provide
    /// the necessary facilities to connect and listen to various aspects of the
    /// setting service. It is expected that there will be new additions to
    /// `Context` as the variety of monitored resources expands.
    #[derive(Clone)]
    pub struct Context {
        pub messenger: monitor::message::TargetedMessenger,
    }

    /// `Generate` defines the closure for generating a monitor. This is
    /// passed into the setting service's environment builder to specify a
    /// participating monitor.
    pub type Generate = Arc<dyn Fn(Context) -> BoxFuture<'static, Result<(), Error>> + Send + Sync>;
}
