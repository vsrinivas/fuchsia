// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_net_dhcp::{Server_Marker, Server_Proxy},
    fuchsia_async as fasync,
};

mod args;
use crate::args::*;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let server = fuchsia_component::client::connect_to_service::<Server_Marker>()
        .with_context(|_| format!("failed to connect to {} service", Server_Marker::DEBUG_NAME))?;
    let () = match argh::from_env() {
        Cli { cmd: Command::Get(get_arg) } => do_get(get_arg, server).await?,
        Cli { cmd: Command::Set(set_arg) } => do_set(set_arg, server).await?,
        Cli { cmd: Command::List(list_arg) } => do_list(list_arg, server).await?,
    };

    Ok(())
}

async fn do_get(get_arg: Get, server: Server_Proxy) -> Result<(), Error> {
    match get_arg.arg {
        GetArg::Option(OptionArg { name }) => {
            let res = server
                .get_option(name.clone().into())
                .await?
                .map_err(|e| fuchsia_zircon::Status::from_raw(e))
                .with_context(|_| format!("get_option({:?}) failed", name))?;
            println!("{:?}", res);
        }
        GetArg::Parameter(ParameterArg { name }) => {
            let res = server
                .get_parameter(name.clone().into())
                .await?
                .map_err(|e| fuchsia_zircon::Status::from_raw(e))
                .with_context(|_| format!("get_parameter({:?}) failed", name))?;
            println!("{:?}", res);
        }
    };
    Ok(())
}

async fn do_set(set_arg: Set, server: Server_Proxy) -> Result<(), Error> {
    match set_arg.arg {
        SetArg::Option(OptionArg { name }) => {
            let () = server
                .set_option(&mut name.clone().into())
                .await?
                .map_err(|e| fuchsia_zircon::Status::from_raw(e))
                .with_context(|_| format!("set_option({:?}) failed", name))?;
        }
        SetArg::Parameter(ParameterArg { name }) => {
            let () = server
                .set_parameter(&mut name.clone().into())
                .await?
                .map_err(|e| fuchsia_zircon::Status::from_raw(e))
                .with_context(|_| format!("set_parameter({:?}) failed", name))?;
        }
    };
    Ok(())
}

async fn do_list(list_arg: List, server: Server_Proxy) -> Result<(), Error> {
    match list_arg.arg {
        ListArg::Option(OptionToken {}) => {
            let res = server
                .list_options()
                .await?
                .map_err(|e| fuchsia_zircon::Status::from_raw(e))
                .with_context(|_| "list_options() failed")?;

            println!("{:?}", res);
        }
        ListArg::Parameter(ParameterToken {}) => {
            let res = server
                .list_parameters()
                .await?
                .map_err(|e| fuchsia_zircon::Status::from_raw(e))
                .with_context(|_| "list_parameters() failed")?;
            println!("{:?}", res);
        }
    };
    Ok(())
}
