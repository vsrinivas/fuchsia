// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_fastboot_common::{is_locked, lock_device, prepare, verify_variable_value},
    ffx_flash_lock_args::FlashLockCommand,
    fidl_fuchsia_developer_bridge::FastbootProxy,
    std::io::{stdout, Write},
};

const LOCKABLE_VAR: &str = "vx-unlockable";
const EPHEMERAL: &str = "ephemeral";
const EPHEMERAL_ERR: &str = "Cannot lock ephemeral devices. Reboot the device to unlock.";
const LOCKED_ERR: &str = "Target is already locked.";
const LOCKED: &str = "locked";

#[ffx_plugin()]
pub async fn flash_lock(fastboot_proxy: FastbootProxy, _cmd: FlashLockCommand) -> Result<()> {
    flash_lock_impl(fastboot_proxy, &mut stdout()).await
}

pub async fn flash_lock_impl<W: Write>(
    fastboot_proxy: FastbootProxy,
    writer: &mut W,
) -> Result<()> {
    prepare(writer, &fastboot_proxy).await?;
    if is_locked(&fastboot_proxy).await? {
        ffx_bail!("{}", LOCKED_ERR);
    }
    if verify_variable_value(LOCKABLE_VAR, EPHEMERAL, &fastboot_proxy).await? {
        ffx_bail!("{}", EPHEMERAL_ERR);
    }
    lock_device(&fastboot_proxy).await?;
    writeln!(writer, "{}", LOCKED)?;
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_developer_bridge::FastbootRequest;
    use std::sync::{Arc, Mutex};

    #[derive(Default)]
    pub(crate) struct FakeServiceCommands {
        pub(crate) variables: Vec<String>,
        pub(crate) oem_commands: Vec<String>,
    }

    pub(crate) fn setup() -> (Arc<Mutex<FakeServiceCommands>>, FastbootProxy) {
        let state = Arc::new(Mutex::new(FakeServiceCommands { ..Default::default() }));
        (
            state.clone(),
            setup_fake_fastboot_proxy(move |req| match req {
                FastbootRequest::Prepare { listener, responder } => {
                    listener.into_proxy().unwrap().on_reboot().unwrap();
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::GetVar { responder, name } => {
                    println!("{}", name);
                    let mut state = state.lock().unwrap();
                    let var = state.variables.pop().unwrap_or("test".to_string());
                    println!("{}", var);
                    responder.send(&mut Ok(var)).unwrap();
                }
                FastbootRequest::Oem { command, responder } => {
                    let mut state = state.lock().unwrap();
                    state.oem_commands.push(command);
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => assert!(false),
            }),
        )
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_locked_device_throws_err() -> Result<()> {
        let (state, proxy) = setup();
        {
            let mut state = state.lock().unwrap();
            // is_locked
            state.variables.push("yes".to_string());
        }
        let mut writer = Vec::<u8>::new();
        let result = flash_lock_impl(proxy, &mut writer).await;
        assert!(result.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_ephemeral_locked_throws_err() -> Result<()> {
        let (state, proxy) = setup();
        {
            let mut state = state.lock().unwrap();
            state.variables.push(EPHEMERAL.to_string());
            // is_locked
            state.variables.push("no".to_string());
        }
        let mut writer = Vec::<u8>::new();
        let result = flash_lock_impl(proxy, &mut writer).await;
        assert!(result.is_err());
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_lock_succeeds() -> Result<()> {
        let (state, proxy) = setup();
        {
            let mut state = state.lock().unwrap();
            // ephemeral
            state.variables.push("whatever".to_string());
            // is_locked
            state.variables.push("no".to_string());
        }
        let mut writer = Vec::<u8>::new();
        flash_lock_impl(proxy, &mut writer).await?;
        let state = state.lock().unwrap();
        assert_eq!(1, state.oem_commands.len());
        assert_eq!("vx-lock", state.oem_commands[0]);
        Ok(())
    }
}
