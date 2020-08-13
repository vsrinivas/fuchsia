// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::prelude::*;
use crate::spinel::*;
use futures::prelude::*;

use anyhow::Error;
use fasync::Time;
use futures::future::ready;

impl<DS: SpinelDeviceClient> SpinelDriver<DS> {
    /// Returns a snapshot of the current connectivity state.
    pub(super) fn get_init_state(&self) -> InitState {
        self.driver_state.lock().init_state
    }

    /// Initialization task.
    ///
    /// This task initializes the device and driver for operation.
    /// If this task was not started because of an unexpected reset,
    /// this method will trigger a reset so that the device can
    /// be initialized to a known defined state.
    ///
    /// This task is executed from `main_loop()`.
    pub(super) async fn init_task(&self) -> Result<(), Error> {
        let update_state_and_check_if_reset_needed = || {
            let old_state;
            let needs_reset;

            {
                let mut driver_state = self.driver_state.lock();
                traceln!("init_task: begin (state = {:?})", driver_state);

                old_state = driver_state.prepare_for_init();

                needs_reset = if driver_state.init_state != InitState::RecoverFromReset {
                    driver_state.init_state = InitState::WaitingForReset;
                    true
                } else {
                    false
                };
            }

            if let Some(old_state) = old_state {
                self.on_connectivity_state_change(ConnectivityState::Attaching, old_state);
            }

            self.driver_state_change.trigger();

            needs_reset
        };

        if update_state_and_check_if_reset_needed() {
            let mut did_reset: bool = false;

            for _i in 1..3 {
                traceln!("init_task: WILL RESET. Attempt {}", _i);

                // Open/Reset the NCP
                traceln!("init_task: Waiting to open.");
                self.device_sink
                    .open()
                    .await
                    .context("Call to `open()` on Spinel device failed.")?;

                traceln!("init_task: Did open.");

                // Wait for the reset to be received.
                did_reset = self
                    .ncp_did_reset
                    .wait()
                    .then(|_| ready(true))
                    .boxed()
                    .on_timeout(Time::after(DEFAULT_TIMEOUT), || false)
                    .await;

                if did_reset {
                    break;
                } else {
                    fx_log_err!("SpinelDriver: Failed to reset");
                }
            }

            if !did_reset {
                Err(format_err!("Failed to reset"))?
            }

            traceln!("init_task: DID RESET.");

            {
                let mut driver_state = self.driver_state.lock();
                driver_state.init_state = InitState::RecoverFromReset;
            }
            self.driver_state_change.trigger();
        }

        traceln!("init_task: Sending get protocol version request...");

        let protocol_version = self
            .get_property_simple::<ProtocolVersion, _>(Prop::ProtocolVersion)
            .await
            .context("Waiting on protocol version")?;

        traceln!("init_task: Protocol version = {:?}", protocol_version);

        match protocol_version {
            ProtocolVersion(PROTOCOL_MAJOR_VERSION, minor) if minor >= PROTOCOL_MINOR_VERSION => {
                fx_log_info!("init_task: Protocol Version: {}.{}", PROTOCOL_MAJOR_VERSION, minor);
            }
            ProtocolVersion(major, minor) => {
                fx_log_err!("init_task: Unsupported Protocol Version: {}.{}", major, minor);
                Err(format_err!("Unsupported Protocol Version: {}.{}", major, minor))?
            }
        };

        traceln!("init_task: Sending get NCP version request...");

        let ncp_version = self.get_property_simple::<String, _>(Prop::NcpVersion).await?;

        fx_log_info!("init_task: NCP Version: {:?}", ncp_version);

        let iface_type = self.get_property_simple::<InterfaceType, _>(Prop::InterfaceType).await?;

        fx_log_info!("init_task: Interface Type: {:?}", iface_type);

        let caps = self.get_property_simple::<Vec<Cap>, _>(Prop::Caps).await?;

        fx_log_info!("init_task: Capabilities: {:?}", caps);

        if let Err(err) = self.frame_handler.send_request(CmdNetRecall).await {
            // This is normally OK, it just means the NCP
            // hasn't been configured for a network yet.
            fx_log_info!("init_task: Unable to recall network: {:?}", err);
        }

        // Properties to fetch/prime, letting their values be processed asynchronously.
        //
        // See SpinelDriver::on_prop_value_is for more information about how these
        // properties are processed.
        //
        // FOR CONSIDERATION WHEN ADDING PROPERTIES TO THE ARRAY: Note that we currently
        // aren't considering any errors generated by these PropValueGet commands as
        // fatal errors. If you need to pre-fetch a property value where any generated
        // error should be considered fatal, you should fetch it explicitly.
        for prop in &[
            Prop::Net(PropNet::NetworkName),
            Prop::Net(PropNet::Role),
            Prop::Net(PropNet::Saved),
            Prop::Net(PropNet::Xpanid),
            Prop::Phy(PropPhy::Chan),
        ] {
            if let Err(err) = self.frame_handler.send_request(CmdPropValueGet(*prop)).await {
                // See the above comment regarding error handling.
                fx_log_err!("init_task: Error fetching {:?}: {:?}", prop, err);
            }
        }

        fx_log_info!("init_task: Finally updating driver state to initialized");

        // Update the driver state.
        {
            let mut driver_state = self.driver_state.lock();
            driver_state.init_state = InitState::Initialized;
            driver_state.caps = caps;
        }
        self.driver_state_change.trigger();

        fx_log_info!("init_task: Finished!");

        Ok(())
    }
}
