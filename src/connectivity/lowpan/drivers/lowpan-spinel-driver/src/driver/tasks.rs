// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::spinel::*;

use anyhow::{Context as _, Error};
use futures::prelude::*;
use lowpan_driver_common::FutureExt;

/// Background Tasks
///
/// These are tasks which are ultimately called from
/// `main_loop()`. They are intended to run in parallel
/// with API-related tasks.
impl<DS: SpinelDeviceClient> SpinelDriver<DS> {
    /// Main loop task that handles the high-level tasks for the driver.
    ///
    /// This task is intended to run continuously and will not normally
    /// terminate. However, it will terminate upon I/O errors and frame
    /// unpacking errors.
    ///
    /// This method must only be invoked once. Invoking it more than once
    /// will cause a panic.
    ///
    /// This method is called from `wrap_inbound_stream()` in `inbound.rs`.
    pub(super) async fn take_main_task(&self) -> Result<(), Error> {
        if self.did_vend_main_task.swap(true, std::sync::atomic::Ordering::Relaxed) {
            panic!("take_main_task must only be called once");
        }
        loop {
            let (init_state, connectivity_state) = {
                let x = self.driver_state.lock();
                (x.init_state, x.connectivity_state)
            };
            match init_state {
                InitState::Initialized if connectivity_state.is_active_and_ready() => {
                    fx_log_info!("main_task: Initialized, active, and ready");

                    let exit_criteria = self.wait_for_state(|x| {
                        x.is_initializing() || !x.connectivity_state.is_active_and_ready()
                    });

                    self.online_task()
                        .boxed()
                        .map(|x| match x {
                            Err(err) if err.is::<Canceled>() => Ok(()),
                            other => other,
                        })
                        .cancel_upon(exit_criteria.boxed(), Ok(()))
                        .map_err(|x| x.context("online_task"))
                        .await?;

                    fx_log_info!("main_task: online_task terminated");
                }

                InitState::Initialized => {
                    fx_log_info!("main_task: Initialized, but either not active or not ready.");

                    let exit_criteria = self.wait_for_state(|x| {
                        x.is_initializing() || x.connectivity_state.is_active_and_ready()
                    });

                    self.offline_task()
                        .boxed()
                        .map(|x| match x {
                            Err(err) if err.is::<Canceled>() => Ok(()),
                            other => other,
                        })
                        .cancel_upon(exit_criteria.boxed(), Ok(()))
                        .map_err(|x| x.context("offline_task"))
                        .await?;

                    fx_log_info!("main_task: offline_task terminated");
                }

                _ => {
                    fx_log_info!("main_task: Uninitialized, starting initialization task");

                    // We are not initialized, start the init task.
                    self.init_task().map_err(|x| x.context("init_task")).await?;
                }
            }
        }
    }

    /// Online loop task that is executed while we are both "ready" and "active".
    ///
    /// This task will bring the device into a state where it
    /// is an active participant in the network.
    ///
    /// The resulting future may be terminated at any time.
    async fn online_task(&self) -> Result<(), Error> {
        fx_log_info!("online_loop: Entered");

        {
            // Wait for our turn.
            let _lock = self.wait_for_api_task_lock().await;

            // Bring up the network interface.
            self.frame_handler
                .send_request(CmdPropValueSet(PropNet::InterfaceUp.into(), true).verify())
                .await?;

            // Bring up the mesh stack.
            self.frame_handler
                .send_request(CmdPropValueSet(PropNet::StackUp.into(), true).verify())
                .await?;
        }

        fx_log_info!("online_loop: Waiting");

        futures::future::pending().await
    }

    /// Offline loop task that is executed while we are either "not ready" or "inactive".
    ///
    /// This task will bring the device to a state where
    /// it is not an active participant in the network.
    ///
    /// The resulting future may be terminated at any time.
    async fn offline_task(&self) -> Result<(), Error> {
        fx_log_info!("offline_loop: Entered");

        {
            // Scope for the API task lock.
            // Wait for our turn.
            let _lock = self.wait_for_api_task_lock().await;

            // Bring down the mesh stack.
            if let Err(err) = self
                .frame_handler
                .send_request(CmdPropValueSet(PropNet::StackUp.into(), false))
                .await
                .context("Setting PropNet::StackUp to False")
            {
                fx_log_err!("Unable to set `PropNet::StackUp`: {:?}", err);
            }

            // Bring down the network interface.
            if let Err(err) = self
                .frame_handler
                .send_request(CmdPropValueSet(PropNet::InterfaceUp.into(), false))
                .await
                .context("Setting PropNet::InterfaceUp to False")
            {
                fx_log_err!("Unable to set `PropNet::InterfaceUp`: {:?}", err);
            }
        } // API task lock goes out of scope here

        fx_log_info!("offline_loop: Waiting");

        futures::future::pending().await
    }
}
