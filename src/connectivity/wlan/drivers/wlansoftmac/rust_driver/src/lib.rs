// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, bail},
    banjo_fuchsia_wlan_common as banjo_common, fuchsia_async as fasync,
    futures::channel::{mpsc, oneshot},
    tracing::{error, info},
    wlan_mlme::{buffer::BufferProvider, device::DeviceInterface},
};

pub struct WlanSoftmacHandle {
    driver_event_sink: mpsc::UnboundedSender<DriverEvent>,
    join_handle: Option<std::thread::JoinHandle<()>>,
}

pub type DriverEvent = wlan_mlme::DriverEvent;

impl WlanSoftmacHandle {
    pub fn stop(&mut self) {
        if let Err(e) = self.driver_event_sink.unbounded_send(DriverEvent::Stop) {
            error!("Failed to signal WlanSoftmac main loop thread to stop: {}", e);
        }
        if let Some(join_handle) = self.join_handle.take() {
            if let Err(e) = join_handle.join() {
                error!("WlanSoftmac main loop thread panicked: {:?}", e);
            }
        }
    }

    pub fn delete(mut self) {
        if self.join_handle.is_some() {
            error!("Called delete on WlanSoftmacHandle without first calling stop");
            self.stop();
        }
    }

    pub fn queue_eth_frame_tx(&mut self, bytes: Vec<u8>) -> Result<(), anyhow::Error> {
        self.driver_event_sink
            .unbounded_send(DriverEvent::EthFrameTx { bytes })
            .map_err(|e| e.into())
    }
}

pub fn start_wlansoftmac(
    device: DeviceInterface,
    buf_provider: BufferProvider,
) -> Result<WlanSoftmacHandle, anyhow::Error> {
    let mut executor = fasync::LocalExecutor::new().unwrap();
    executor.run_singlethreaded(start_wlansoftmac_async(device, buf_provider))
}

/// This is a helper function for running wlansoftmac inside a test. For non-test
/// use cases, it should generally be invoked via `start_wlansoftmac`.
async fn start_wlansoftmac_async(
    device: DeviceInterface,
    buf_provider: BufferProvider,
) -> Result<WlanSoftmacHandle, anyhow::Error> {
    let (driver_event_sink, driver_event_stream) = mpsc::unbounded();
    let (startup_sender, startup_receiver) = oneshot::channel();

    let driver_event_sink_clone = driver_event_sink.clone();
    info!("Spawning wlansoftmac main loop thread.");
    let join_handle = std::thread::spawn(move || {
        let mut executor = fasync::LocalExecutor::new().unwrap();
        let future = wlansoftmac_thread(
            device,
            buf_provider,
            driver_event_sink_clone,
            driver_event_stream,
            startup_sender,
        );
        executor.run_singlethreaded(future);
    });

    match startup_receiver.await.map_err(|e| anyhow::Error::from(e)) {
        Ok(Ok(())) => Ok(WlanSoftmacHandle { driver_event_sink, join_handle: Some(join_handle) }),
        Err(err) | Ok(Err(err)) => match join_handle.join() {
            Ok(()) => bail!("Failed to start the wlansoftmac event loop: {:?}", err),
            Err(panic_err) => {
                bail!("wlansoftmac event loop failed and then panicked: {}, {:?}", err, panic_err)
            }
        },
    }
}

async fn wlansoftmac_thread(
    device: DeviceInterface,
    buf_provider: BufferProvider,
    driver_event_sink: mpsc::UnboundedSender<DriverEvent>,
    driver_event_stream: mpsc::UnboundedReceiver<DriverEvent>,
    startup_sender: oneshot::Sender<Result<(), anyhow::Error>>,
) {
    let softmac_info = device.wlan_softmac_info();
    match softmac_info.mac_role {
        banjo_common::WlanMacRole::CLIENT => {
            info!("Running wlansoftmac with client role");
            let config = wlan_mlme::client::ClientConfig {
                ensure_on_channel_time: fasync::Duration::from_millis(500).into_nanos(),
            };
            wlan_mlme::mlme_main_loop::<wlan_mlme::client::ClientMlme>(
                config,
                device,
                buf_provider,
                driver_event_sink,
                driver_event_stream,
                startup_sender,
            )
            .await;
        }
        banjo_common::WlanMacRole::AP => {
            info!("Running wlansoftmac with AP role");
            let config = ieee80211::Bssid(softmac_info.sta_addr);
            wlan_mlme::mlme_main_loop::<wlan_mlme::ap::Ap>(
                config,
                device,
                buf_provider,
                driver_event_sink,
                driver_event_stream,
                startup_sender,
            )
            .await;
        }
        unsupported => {
            error!("Unsupported mac role: {:?}", unsupported);
            return;
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        wlan_mlme::{self, device::test_utils::FakeDevice},
    };

    #[test]
    fn test_wlansoftmac_delete_no_crash() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut fake_device = FakeDevice::new(&mut exec);
        let fake_buf_provider = wlan_mlme::buffer::FakeBufferProvider::new();
        let handle_fut = start_wlansoftmac_async(fake_device.as_raw_device(), fake_buf_provider);
        let mut handle = exec.run_singlethreaded(handle_fut).expect("Failed to start wlansoftmac");
        handle.stop();
        handle.delete();
    }

    #[test]
    fn test_wlansoftmac_delete_without_stop_no_crash() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut fake_device = FakeDevice::new(&mut exec);
        let fake_buf_provider = wlan_mlme::buffer::FakeBufferProvider::new();
        let handle_fut = start_wlansoftmac_async(fake_device.as_raw_device(), fake_buf_provider);
        let handle = exec.run_singlethreaded(handle_fut).expect("Failed to start wlansoftmac");
        handle.delete();
    }

    #[test]
    fn test_wlansoftmac_handle_use_after_stop() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let mut fake_device = FakeDevice::new(&mut exec);
        let fake_buf_provider = wlan_mlme::buffer::FakeBufferProvider::new();
        let handle_fut = start_wlansoftmac_async(fake_device.as_raw_device(), fake_buf_provider);
        let mut handle = exec.run_singlethreaded(handle_fut).expect("Failed to start wlansoftmac");

        handle
            .queue_eth_frame_tx(vec![0u8; 10])
            .expect("Should be able to queue tx before stopping wlansoftmac");
        handle.stop();
        handle
            .queue_eth_frame_tx(vec![0u8; 10])
            .expect_err("Shouldn't be able to queue tx after stopping wlansoftmac");
    }
}
