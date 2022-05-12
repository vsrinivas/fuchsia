// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    ffx_debug_limbo_args::{LimboCommand, LimboSubCommand},
    fidl_fuchsia_exception::ProcessLimboProxy,
    fuchsia_zircon_status::Status,
    fuchsia_zircon_types::{ZX_ERR_NOT_FOUND, ZX_ERR_UNAVAILABLE},
};

#[ffx_core::ffx_plugin(ProcessLimboProxy = "core/exceptions:expose:fuchsia.exception.ProcessLimbo")]
pub async fn plugin_main(limbo_proxy: ProcessLimboProxy, cmd: LimboCommand) -> Result<()> {
    match cmd.command {
        LimboSubCommand::Status(_) => status(limbo_proxy).await,
        LimboSubCommand::Enable(_) => enable(limbo_proxy).await,
        LimboSubCommand::Disable(_) => disable(limbo_proxy).await,
        LimboSubCommand::List(_) => list(limbo_proxy).await,
        LimboSubCommand::Release(release_cmd) => release(limbo_proxy, release_cmd.pid).await,
    }
}

async fn status(limbo_proxy: ProcessLimboProxy) -> Result<()> {
    let active = limbo_proxy.get_active().await?;
    if active {
        println!("Limbo is active.");
    } else {
        println!("Limbo is not active.");
    }
    Ok(())
}

async fn enable(limbo_proxy: ProcessLimboProxy) -> Result<()> {
    let active = limbo_proxy.get_active().await?;
    if active {
        println!("Limbo is already active.");
    } else {
        limbo_proxy.set_active(true).await?;
        println!("Activated the process limbo.");
    }
    Ok(())
}

async fn disable(limbo_proxy: ProcessLimboProxy) -> Result<()> {
    let active = limbo_proxy.get_active().await?;
    if !active {
        println!("Limbo is already deactivated.");
    } else {
        limbo_proxy.set_active(false).await?;
        println!("Deactivated the process limbo. All contained processes have been freed.");
    }
    Ok(())
}

async fn list(limbo_proxy: ProcessLimboProxy) -> Result<()> {
    match limbo_proxy
        .list_processes_waiting_on_exception()
        .await
        .context("FIDL error in list_processes_waiting_on_exception")?
    {
        Ok(exceptions) => {
            if exceptions.is_empty() {
                println!("No processes currently on limbo.");
            } else {
                println!("Processes currently on limbo:");
                for metadada in exceptions {
                    let info = metadada.info.expect("missing info");
                    let process_name = metadada.process_name.expect("missing process_name");
                    let thread_name = metadada.thread_name.expect("missing thread_name");
                    println!(
                        "- {} (pid: {}), thread {} (tid: {}) on exception: {:?}",
                        process_name, info.process_koid, thread_name, info.thread_koid, info.type_
                    );
                }
            }
        }
        Err(e) => {
            if e == ZX_ERR_UNAVAILABLE {
                println!("Process limbo is not active.");
            } else {
                println!("Could not list the process limbo: {:?}", Status::from_raw(e));
            }
        }
    }
    Ok(())
}

async fn release(limbo_proxy: ProcessLimboProxy, pid: u64) -> Result<()> {
    match limbo_proxy.release_process(pid).await? {
        Ok(_) => println!("Successfully release process {} from limbo.", pid),
        Err(e) => match e {
            ZX_ERR_UNAVAILABLE => println!("Process limbo is not active."),
            ZX_ERR_NOT_FOUND => println!("Could not find pid {} in limbo.", pid),
            e => {
                println!("Could not release process {} from limbo: {:?}", pid, Status::from_raw(e))
            }
        },
    }
    Ok(())
}
