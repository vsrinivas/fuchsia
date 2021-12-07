// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_debug_implementations)]
#![warn(rust_2018_idioms)]
#![warn(clippy::all)]

mod async_condition;
mod dummy_device;
mod lowpan_device;
mod register;
mod serve_to;

pub mod net;
pub mod spinel;

#[cfg(test)]
mod tests;

pub use async_condition::*;
pub use dummy_device::DummyDevice;
pub use lowpan_device::Driver;
pub use register::*;
pub use serve_to::*;

// NOTE: This line is a hack to work around some issues
//       with respect to external rust crates.
use spinel_pack::{self as spinel_pack};

#[macro_export]
macro_rules! traceln (($($args:tt)*) => { fuchsia_syslog::macros::fx_log_trace!($($args)*); }; );

#[macro_use]
pub(crate) mod prelude_internal {
    pub use traceln;

    pub use fidl::prelude::*;
    pub use futures::prelude::*;
    pub use spinel_pack::prelude::*;

    pub use fuchsia_syslog::macros::*;

    pub use crate::ServeTo as _;
    pub use crate::{ZxResult, ZxStatus};
    pub use anyhow::{format_err, Context as _};
    pub use async_trait::async_trait;
    pub use fasync::TimeoutExt as _;
    pub use fidl_fuchsia_net_ext as fnet_ext;
    pub use fuchsia_async as fasync;

    pub use net_declare::{fidl_ip, fidl_ip_v6};
}

pub use fuchsia_zircon_status::Status as ZxStatus;

/// A `Result` that uses `fuchsia_zircon::Status` for the error condition.
pub type ZxResult<T = ()> = Result<T, ZxStatus>;

const MAX_CONCURRENT: usize = 100;
