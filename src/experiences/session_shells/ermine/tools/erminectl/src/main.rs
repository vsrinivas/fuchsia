// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    // anyhow::Error,
    argh::FromArgs,
    fidl_ermine_tools as fermine,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::client::{connect_to_protocol_at_dir_root, connect_to_protocol_at_path},
};

#[derive(FromArgs, Debug, PartialEq)]
/// Various operations to control Ermine user experience.
pub struct Args {
    #[argh(subcommand)]
    pub command: Option<Command>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum Command {
    Oobe(OobeCommand),
    Shell(ShellCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "oobe")]
/// Control OOBE UI.
pub struct OobeCommand {
    #[argh(subcommand)]
    pub command: Option<OobeSubCommand>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum OobeSubCommand {
    Password(OobeSetPasswordCommand),
    Login(OobeLoginCommand),
    Skip(OobeSkipCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "set_password")]
/// Create password in OOBE
pub struct OobeSetPasswordCommand {
    #[argh(positional)]
    pub password: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "login")]
/// Login password in OOBE
pub struct OobeLoginCommand {
    #[argh(positional)]
    pub password: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "skip")]
/// Skip current screen.
pub struct OobeSkipCommand {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "shell")]
/// Control shell UI.
pub struct ShellCommand {
    #[argh(subcommand)]
    pub command: ShellSubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum ShellSubCommand {
    Launch(ShellLaunchCommand),
    CloseAll(ShellCloseAllCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "launch")]
/// Launch an application.
pub struct ShellLaunchCommand {
    #[argh(positional)]
    pub app_name: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "closeAll")]
/// Close all running applications.
pub struct ShellCloseAllCommand {}

/// Moniker of login_shell component
const LOGIN_SHELL_MONIKER: &str =
    "./core/session-manager/session:session/workstation_session/login_shell";

/// Moniker of ermine_shell component
const ERMINE_SHELL_MONIKER: &str =
    "./core/session-manager/session:session/workstation_session/login_shell/ermine_shell";

async fn connect_to_exposed_protocol<P: fidl::endpoints::DiscoverableProtocolMarker>(
    realm_query: &fsys::RealmQueryProxy,
    moniker: &str,
) -> Result<P::Proxy, Error> {
    let resolved_dirs = realm_query
        .get_instance_directories(moniker)
        .await?
        .map_err(|e| format_err!("RealmQuery error: {:?}", e))?
        .ok_or(format_err!("{} is not resolved", moniker))?;
    let exposed_dir = resolved_dirs.exposed_dir.into_proxy()?;
    connect_to_protocol_at_dir_root::<P>(&exposed_dir)
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let Args { command } = argh::from_env();
    let realm_query =
        connect_to_protocol_at_path::<fsys::RealmQueryMarker>("/svc/fuchsia.sys2.RealmQuery.root")
            .unwrap();

    let oobe_automator = connect_to_exposed_protocol::<fermine::OobeAutomatorMarker>(
        &realm_query,
        LOGIN_SHELL_MONIKER,
    )
    .await?;

    match command {
        None => {
            let page = oobe_automator.get_oobe_page().await?;
            match page {
                fermine::OobePage::Shell => println!("Shell"),
                _ => println!("Oobe"),
            }
        }
        Some(Command::Oobe(OobeCommand { command })) => match command {
            None => {
                let page = oobe_automator.get_oobe_page().await?;
                println!("{:?}", page);
            }
            Some(OobeSubCommand::Login(OobeLoginCommand { password })) => {
                let result = oobe_automator.login(&password).await?;
                match result {
                    Ok(()) => println!("ok"),
                    _ => result
                        .map_err(|err: fermine::AutomatorErrorCode| format_err!("{:?}", err))?,
                }
            }
            Some(OobeSubCommand::Password(OobeSetPasswordCommand { password })) => {
                let result = oobe_automator.set_password(&password).await?;
                match result {
                    Ok(()) => println!("ok"),
                    _ => result
                        .map_err(|err: fermine::AutomatorErrorCode| format_err!("{:?}", err))?,
                }
            }
            Some(OobeSubCommand::Skip(OobeSkipCommand {})) => {
                let result = oobe_automator.skip_page().await?;
                match result {
                    Ok(()) => println!("ok"),
                    _ => result
                        .map_err(|err: fermine::AutomatorErrorCode| format_err!("{:?}", err))?,
                }
            }
        },
        Some(Command::Shell(ShellCommand { command })) => {
            match command {
                ShellSubCommand::Launch(ShellLaunchCommand { app_name }) => {
                    let shell_automator = connect_to_exposed_protocol::<
                        fermine::ShellAutomatorMarker,
                    >(&realm_query, ERMINE_SHELL_MONIKER)
                    .await?;
                    let result = shell_automator
                        .launch(fermine::ShellAutomatorLaunchRequest {
                            app_name: Some(app_name),
                            ..fermine::ShellAutomatorLaunchRequest::EMPTY
                        })
                        .await?;
                    match result {
                        Ok(()) => println!("ok"),
                        _ => result
                            .map_err(|err: fermine::AutomatorErrorCode| format_err!("{:?}", err))?,
                    }
                }
                ShellSubCommand::CloseAll(ShellCloseAllCommand {}) => {
                    let shell_automator = connect_to_exposed_protocol::<
                        fermine::ShellAutomatorMarker,
                    >(&realm_query, ERMINE_SHELL_MONIKER)
                    .await?;
                    let result = shell_automator.close_all().await?;
                    match result {
                        Ok(()) => println!("ok"),
                        _ => result
                            .map_err(|err: fermine::AutomatorErrorCode| format_err!("{:?}", err))?,
                    }
                }
            }
        }
    }
    Ok(())
}
