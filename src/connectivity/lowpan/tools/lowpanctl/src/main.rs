// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod context;
pub mod dataset_command;
pub mod energy_scan_command;
pub mod form_command;
pub mod get_counters_command;
pub mod get_credential;
pub mod get_external_routes_command;
pub mod get_mac_filter_settings_command;
pub mod get_neighbor_table_command;
pub mod get_on_mesh_nets_command;
pub mod get_supported_channels;
pub mod get_supported_network_types;
pub mod invocation;
pub mod join_command;
pub mod leave_command;
pub mod list_command;
pub mod make_joinable_command;
pub mod mfg_command;
pub mod network_scan_command;
pub mod provision_command;
pub mod register_external_route_command;
pub mod register_on_mesh_net_command;
pub mod repeat_command;
pub mod replace_mac_filter_settings_command;
pub mod reset_command;
pub mod set_active_comamnd;
pub mod status_command;
pub mod unregister_external_route_command;
pub mod unregister_on_mesh_net_command;

#[macro_use]
mod prelude {
    #![allow(unused_imports)]

    pub use futures::prelude::*;

    pub use anyhow::{format_err, Context as _, Error};
    pub use argh::FromArgs;
    pub use fidl::endpoints::create_endpoints;
    pub use fuchsia_async as fasync;
    pub use fuchsia_component::client::connect_to_protocol;
    pub use std::convert::TryInto as _;
}

use context::*;
use invocation::*;
use prelude::*;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: LowpanCtlInvocation = argh::from_env();
    let mut context: LowpanCtlContext = LowpanCtlContext::from_invocation(&args)?;

    args.exec(&mut context).await?;

    Ok(())
}
