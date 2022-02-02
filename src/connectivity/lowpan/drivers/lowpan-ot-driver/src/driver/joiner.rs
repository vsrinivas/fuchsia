// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use anyhow::Error;
use fidl_fuchsia_lowpan::*;
use fidl_fuchsia_lowpan_device::*;
use futures::future::Either;
use lowpan_driver_common::AsyncConditionWait;
use lowpan_driver_common::ZxResult;

impl<OT, NI> OtDriver<OT, NI>
where
    OT: Send + ot::InstanceInterface,
    NI: NetworkInterface,
{
    pub(super) fn joiner_start(
        &self,
        joiner_params: JoinerCommissioningParams,
    ) -> BoxStream<'_, ZxResult<Result<ProvisioningProgress, ProvisionError>>> {
        use futures::channel::oneshot::*;

        // This one-shot channel is used to get the error back from the call to `joiner_start`.
        let (sender, receiver) = channel();

        let init_task = async move {
            // Wait until we are not busy.
            self.wait_for_state(|x| !x.is_busy()).await;

            // For in-band joiner commissioning, pskd is required.
            if joiner_params.pskd.as_ref().map(|x| x.is_empty()).unwrap_or(true) {
                fx_log_err!("join network: pskd is empty");
                return Err(Error::from(ZxStatus::INVALID_ARGS));
            }

            if !self.get_connectivity_state().is_active() {
                fx_log_err!("join network: interface not enabled/active");
                return Err(Error::from(ZxStatus::BAD_STATE));
            }

            let cleanup_func = CleanupFunc(Some(move || {
                fx_log_debug!("Running Join Cleanup Func");
                let driver_state = self.driver_state.lock();
                driver_state.ot_instance.joiner_stop();
            }));

            {
                let driver_state = self.driver_state.lock();

                // Bring up the network interface on OpenThread.
                driver_state.ot_instance.ip6_set_enabled(true).context("ip6_set_enabled")?;

                // Start joining
                driver_state.ot_instance.joiner_start(
                    joiner_params.pskd.unwrap().as_str(),
                    joiner_params.provisioning_url.as_deref(),
                    joiner_params.vendor_name.as_deref(),
                    joiner_params.vendor_model.as_deref(),
                    joiner_params.vendor_sw_version.as_deref(),
                    joiner_params.vendor_data_string.as_deref(),
                    |result| {
                        let _ = sender.send(result);
                    },
                )?;
            }

            Ok(cleanup_func)
        };

        let stream = futures::stream::unfold(
            (receiver, false, None),
            move |last_state: (
                Receiver<ot::Result>, // Our join result future.
                bool,                 // Indicates if we should close.
                Option<(
                    ot::JoinerState,        // The previous joiner state.
                    AsyncConditionWait<'_>, // Our outstanding condition to wait on.
                )>,
            )| {
                async move {
                    // Check to see if the stream should be closed.
                    if last_state.1 {
                        return None;
                    }

                    // Extract our receiver, to make for
                    // easier reading.
                    let mut receiver = last_state.0;

                    if let Some((last_state, mut condition)) = last_state.2 {
                        loop {
                            // We use references to futures so that we don't consume them.
                            let future = futures::future::select(&mut condition, &mut receiver);

                            // Wait for the driver state change condition or the receiver to unblock.
                            match future.await {
                                // The `joiner_start` call has finished with a result.
                                Either::Right((result, _)) => {
                                    return Some((
                                        Ok(match result {
                                            Ok(Ok(())) => Ok(ProvisioningProgress::Identity(
                                                self.driver_state.lock().get_current_identity(),
                                            )),
                                            Ok(Err(x)) => Err(x.into_ext()),
                                            Err(_) => Err(ProvisionError::Canceled),
                                        }),
                                        (receiver, true, None),
                                    ));
                                }

                                // The driver state has changed, so we check to
                                // see if the joiner state has changed.
                                Either::Left(_) => {
                                    // Set up the condition for the next iteration.
                                    condition = self.driver_state_change.wait();

                                    // Grab our next joiner state.
                                    let snapshot =
                                        self.driver_state.lock().ot_instance.joiner_get_state();

                                    if snapshot != last_state {
                                        // Convert our joiner state into a progress indicator
                                        // and pass along enough start for our next run.
                                        return Some((
                                            Ok(Ok(snapshot.into_ext())),
                                            (receiver, false, Some((snapshot, condition))),
                                        ));
                                    }
                                }
                            } // match
                        } // loop
                    } else {
                        // This is the first item being emitted from the stream,
                        // so we end up emitting first progress report and
                        // set ourselves up for the next iteration.
                        let condition = self.driver_state_change.wait();
                        let snapshot = self.driver_state.lock().ot_instance.joiner_get_state();
                        Some((
                            Ok(Ok(snapshot.into_ext())),
                            (receiver, false, Some((snapshot, condition))),
                        ))
                    }
                }
            },
        );

        self.start_ongoing_stream_process(init_task, stream, fasync::Time::after(JOIN_TIMEOUT))
            .boxed()
    }
}
