// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    // anyhow::Error,
    argh::FromArgs,
    fidl_ermine_tools as fermine,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol_at,
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

/// Path to protocols exposed by session-manager.
const LOGIN_SHELL_EXPOSED: &str = "/hub-v2/children/core/children/session-manager/children/session:session/children/workstation_session/children/login_shell/exec/expose";

/// Path to protocols exposed by the session.
const ERMINE_SHELL_EXPOSED: &str =
    "/hub-v2/children/core/children/session-manager/children/session:session/children/workstation_session/children/login_shell/children/ermine_shell/exec/expose";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let Args { command } = argh::from_env();
    let oobe_automator =
        connect_to_protocol_at::<fermine::OobeAutomatorMarker>(LOGIN_SHELL_EXPOSED)?;

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
        Some(Command::Shell(ShellCommand { command })) => match command {
            ShellSubCommand::Launch(ShellLaunchCommand { app_name }) => {
                let shell_automator =
                    connect_to_protocol_at::<fermine::ShellAutomatorMarker>(ERMINE_SHELL_EXPOSED)?;
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
                let shell_automator =
                    connect_to_protocol_at::<fermine::ShellAutomatorMarker>(ERMINE_SHELL_EXPOSED)?;
                let result = shell_automator.close_all().await?;
                match result {
                    Ok(()) => println!("ok"),
                    _ => result
                        .map_err(|err: fermine::AutomatorErrorCode| format_err!("{:?}", err))?,
                }
            }
        },
    }
    Ok(())
}
