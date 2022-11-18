// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate implements IEEE Std 802.11-2016 MLME as a library for hardware that supports
//! SoftMAC. This is distinct from FullMAC, which is implemented by drivers and firmware. The
//! implementation is broadly divided between client and AP stations, with some shared components
//! and state machine infrastructure. See the [`client`] and [`ap`] modules.
//!
//! [`ap`]: crate::ap
//! [`client`]: crate::client

mod akm_algorithm;
pub mod ap;
pub mod auth;
mod block_ack;
pub mod buffer;
pub mod client;
mod ddk_converter;
pub mod device;
pub mod disconnect;
pub mod error;
pub mod key;
mod logger;
mod minstrel;
#[allow(unused)] // TODO(fxbug.dev/79543): Remove annotation once used.
mod probe_sequence;

pub use {ddk_converter::*, wlan_common as common};

use {
    anyhow::{anyhow, bail, Error},
    banjo_fuchsia_hardware_wlan_softmac as banjo_wlan_softmac,
    banjo_fuchsia_wlan_common as banjo_common,
    device::{Device, DeviceInterface},
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    futures::{
        channel::{mpsc, oneshot},
        select, StreamExt,
    },
    log::{error, info},
    parking_lot::Mutex,
    std::sync::Arc,
    std::time::Duration,
};

pub trait MlmeImpl {
    type Config: Send;
    type TimerEvent;
    fn new(
        config: Self::Config,
        device: Device,
        buf_provider: buffer::BufferProvider,
        scheduler: common::timer::Timer<Self::TimerEvent>,
    ) -> Self;
    fn handle_mlme_message(&mut self, msg: fidl_mlme::MlmeRequest) -> Result<(), Error>;
    fn handle_mac_frame_rx(&mut self, bytes: &[u8], rx_info: banjo_wlan_softmac::WlanRxInfo);
    fn handle_eth_frame_tx(&mut self, bytes: &[u8]) -> Result<(), Error>;
    fn handle_scan_complete(&mut self, status: zx::Status, scan_id: u64);
    fn handle_timeout(&mut self, event_id: common::timer::EventId, event: Self::TimerEvent);
    fn access_device(&mut self) -> &mut Device;
}

pub struct MinstrelTimer {
    timer: wlan_common::timer::Timer<()>,
    current_timer: Option<common::timer::EventId>,
}

impl minstrel::TimerManager for MinstrelTimer {
    fn schedule(&mut self, from_now: Duration) {
        self.current_timer.replace(self.timer.schedule_after(from_now.into(), ()));
    }
    fn cancel(&mut self) {
        self.current_timer.take();
    }
}

type MinstrelWrapper = Arc<Mutex<minstrel::MinstrelRateSelector<MinstrelTimer>>>;

// DriverEventSink is used by other devices to interact with our main loop thread. All
// events from our ethernet device or vendor device are converted to DriverEvents
// and sent through this sink, where they can then be handled serially. Multiple copies of
// DriverEventSink may be safely passed between threads, including one that is used by our
// vendor driver as the context for wlan_softmac_ifc_protocol_ops.
struct DriverEventSink(pub mpsc::UnboundedSender<DriverEvent>);

// TODO(fxbug.dev/29063): Remove copies from MacFrame and EthFrame.
pub enum DriverEvent {
    // Indicates that the device is being removed and our main loop should exit.
    Stop,
    // TODO(fxbug.dev/43456): We need to keep stats for these events and respond to StatsQueryRequest.
    // Indicates receipt of a MAC frame from a peer.
    MacFrameRx { bytes: Vec<u8>, rx_info: banjo_wlan_softmac::WlanRxInfo },
    // Requests transmission of an ethernet frame over the air.
    EthFrameTx { bytes: Vec<u8> },
    // Reports a scan is complete.
    ScanComplete { status: zx::Status, scan_id: u64 },
    // Reports the result of an attempted frame transmission.
    TxStatusReport { tx_status: banjo_common::WlanTxStatus },
    // Reports the current status of the vendor driver.
    Status { status: u32 },
}

fn should_enable_minstrel(mac_sublayer: &banjo_common::MacSublayerSupport) -> bool {
    mac_sublayer.device.tx_status_report_supported && !mac_sublayer.rate_selection_offload.supported
}

const MINSTREL_UPDATE_INTERVAL: std::time::Duration = std::time::Duration::from_millis(100);
// Remedy for fxbug.dev/8165 (fxbug.dev/33151)
// See |DATA_FRAME_INTERVAL_NANOS|
// in //src/connectivity/wlan/testing/hw-sim/test/rate_selection/src/lib.rs
// Ensure at least one probe frame (generated every 16 data frames)
// in every cycle:
// 16 <= (MINSTREL_UPDATE_INTERVAL_HW_SIM / MINSTREL_DATA_FRAME_INTERVAL_NANOS * 1e6) < 32.
const MINSTREL_UPDATE_INTERVAL_HW_SIM: std::time::Duration = std::time::Duration::from_millis(83);

pub async fn mlme_main_loop<T: MlmeImpl>(
    config: T::Config,
    device: DeviceInterface,
    buf_provider: buffer::BufferProvider,
    driver_event_sink: mpsc::UnboundedSender<DriverEvent>,
    driver_event_stream: mpsc::UnboundedReceiver<DriverEvent>,
    startup_sender: oneshot::Sender<Result<(), Error>>,
) {
    let mut driver_event_sink = Box::new(DriverEventSink(driver_event_sink));
    let ifc = device::WlanSoftmacIfcProtocol::new(driver_event_sink.as_mut());
    // Indicate to the vendor driver that we can start sending and receiving info. Any messages received from the
    // driver before we start our MLME will be safely buffered in our driver_event_sink.
    // Note that device.start will copy relevant fields out of ifc, so dropping it after this is fine.
    // The returned value is the MLME server end of the channel wlanmevicemonitor created to connect MLME and SME.
    let mlme_protocol_handle_via_iface_creation = match device.start(&ifc) {
        Ok(handle) => handle,
        Err(e) => {
            // Failure to unwrap indicates a critical failure in the driver init thread.
            startup_sender.send(Err(anyhow!("device.start failed: {}", e))).unwrap();
            return;
        }
    };
    let channel = zx::Channel::from(mlme_protocol_handle_via_iface_creation);
    let server = fidl::endpoints::ServerEnd::<fidl_mlme::MlmeMarker>::new(channel);
    let (mlme_request_stream, control_handle) = match server.into_stream_and_control_handle() {
        Ok(res) => res,
        Err(e) => {
            // Failure to unwrap indicates a critical failure in the driver init thread.
            startup_sender.send(Err(anyhow!("Failed to get MLME request stream: {}", e))).unwrap();
            return;
        }
    };

    let device_mac_sublayer_support = device.mac_sublayer_support();
    let (minstrel_timer, minstrel_time_stream) = common::timer::create_timer();
    let update_interval = if device_mac_sublayer_support.device.is_synthetic {
        MINSTREL_UPDATE_INTERVAL_HW_SIM
    } else {
        MINSTREL_UPDATE_INTERVAL
    };
    let minstrel = if should_enable_minstrel(&device_mac_sublayer_support) {
        let timer_manager = MinstrelTimer { timer: minstrel_timer, current_timer: None };
        let probe_sequence = probe_sequence::ProbeSequence::random_new();
        Some(Arc::new(Mutex::new(minstrel::MinstrelRateSelector::new(
            timer_manager,
            update_interval,
            probe_sequence,
        ))))
    } else {
        None
    };
    let new_device = Device::new(device, minstrel.clone(), control_handle);
    let (timer, time_stream) = common::timer::create_timer();

    let mlme_impl = T::new(config, new_device, buf_provider, timer);

    // Startup is complete. Signal the main thread to proceed.
    // Failure to unwrap indicates a critical failure in the driver init thread.
    startup_sender.send(Ok(())).unwrap();

    let result = main_loop_impl(
        mlme_impl,
        minstrel,
        mlme_request_stream,
        driver_event_stream,
        time_stream,
        minstrel_time_stream,
    )
    .await;
    match result {
        Ok(()) => info!("MLME event loop exited gracefully."),
        Err(e) => error!("MLME event loop exited with error: {:?}", e),
    }
}

/// Begin processing MLME events.
/// Does not return until iface destruction is requested via DriverEvent::Stop, unless
/// a critical error occurs. Note that MlmeHandle::stop will work in either case.
async fn main_loop_impl<T: MlmeImpl>(
    mut mlme_impl: T,
    minstrel: Option<MinstrelWrapper>,
    // A stream of requests coming from the parent SME of this MLME.
    mut mlme_request_stream: fidl_mlme::MlmeRequestStream,
    // A stream of events initiated by C++ device drivers and then buffered here
    // by our MlmeHandle.
    mut driver_event_stream: mpsc::UnboundedReceiver<DriverEvent>,
    time_stream: common::timer::TimeStream<T::TimerEvent>,
    minstrel_time_stream: common::timer::TimeStream<()>,
) -> Result<(), Error> {
    let mut timer_stream = common::timer::make_async_timed_event_stream(time_stream).fuse();
    let mut minstrel_timer_stream =
        common::timer::make_async_timed_event_stream(minstrel_time_stream).fuse();
    loop {
        select! {
            // Process requests from SME.
            mlme_request = mlme_request_stream.next() => match mlme_request {
                Some(req) => {
                    match req {
                        Ok(req) => {
                            let method_name = req.method_name();
                            if let Err(e) = mlme_impl.handle_mlme_message(req) {
                                info!("Failed to handle mlme {} request: {}", method_name, e);
                            }
                        }
                        Err(e) => {
                            info!("Failure while receiving mlme request: {}", e);
                        }
                    }

                }
                None => bail!("MLME request stream terminated unexpectedly."),
            },
            // Process requests from our C++ drivers.
            driver_event = driver_event_stream.next() => match driver_event {
                Some(event) => match event {
                    // DriverEvent::Stop indicates a safe shutdown.
                    DriverEvent::Stop => return Ok(()),
                    DriverEvent::MacFrameRx { bytes, rx_info } => {
                        mlme_impl.handle_mac_frame_rx(&bytes[..], rx_info);
                    }
                    DriverEvent::EthFrameTx { bytes } => {
                        if let Err(e) = mlme_impl.handle_eth_frame_tx(&bytes[..]) {
                            // TODO(fxbug.dev/45464): Keep a counter of these failures.
                            info!("Failed to handle eth frame: {}", e);
                        }
                    }
                    DriverEvent::ScanComplete { status, scan_id } => {
                        mlme_impl.handle_scan_complete(status, scan_id)
                    },
                    DriverEvent::TxStatusReport { tx_status } => {
                        if let Some(minstrel) = minstrel.as_ref() {
                            minstrel.lock().handle_tx_status_report(&tx_status)
                        }
                    }
                    DriverEvent::Status { status } => {
                        mlme_impl.access_device().set_eth_status(status)
                    }
                },
                None => bail!("Driver event stream terminated unexpectedly."),
            },
            timed_event = timer_stream.select_next_some() => {
                mlme_impl.handle_timeout(timed_event.id, timed_event.event);
            }
            _minstrel_timeout = minstrel_timer_stream.select_next_some() => {
                if let Some(minstrel) = minstrel.as_ref() {
                    minstrel.lock().handle_timeout()
                }
            }
        }
    }
}

#[cfg(test)]
pub mod test_utils {
    use {
        super::*, banjo_fuchsia_hardware_wlan_associnfo as banjo_wlan_associnfo,
        banjo_fuchsia_wlan_common as banjo_common, fidl::endpoints::RequestStream,
        fuchsia_async as fasync, wlan_common::channel,
    };

    pub struct FakeMlme {
        device: Device,
    }

    impl MlmeImpl for FakeMlme {
        type Config = ();

        type TimerEvent = ();

        fn new(
            _config: Self::Config,
            device: Device,
            _buf_provider: buffer::BufferProvider,
            _scheduler: wlan_common::timer::Timer<Self::TimerEvent>,
        ) -> Self {
            Self { device }
        }

        fn handle_mlme_message(
            &mut self,
            _msg: fidl_mlme::MlmeRequest,
        ) -> Result<(), anyhow::Error> {
            unimplemented!()
        }

        fn handle_mac_frame_rx(&mut self, _bytes: &[u8], _rx_info: banjo_wlan_softmac::WlanRxInfo) {
            unimplemented!()
        }

        fn handle_eth_frame_tx(&mut self, _bytes: &[u8]) -> Result<(), anyhow::Error> {
            unimplemented!()
        }

        fn handle_scan_complete(&mut self, _status: zx::Status, _scan_id: u64) {
            unimplemented!()
        }

        fn handle_timeout(
            &mut self,
            _event_id: wlan_common::timer::EventId,
            _event: Self::TimerEvent,
        ) {
            unimplemented!()
        }

        fn access_device(&mut self) -> &mut Device {
            &mut self.device
        }
    }

    pub(crate) fn fake_wlan_channel() -> channel::Channel {
        channel::Channel { primary: 1, cbw: channel::Cbw::Cbw20 }
    }

    #[derive(Copy, Clone, Debug)]
    pub struct MockWlanRxInfo {
        pub rx_flags: banjo_wlan_softmac::WlanRxInfoFlags,
        pub valid_fields: u32,
        pub phy: banjo_common::WlanPhyType,
        pub data_rate: u32,
        pub channel: banjo_common::WlanChannel,
        pub mcs: u8,
        pub rssi_dbm: i8,
        pub snr_dbh: i16,
    }

    impl MockWlanRxInfo {
        pub(crate) fn with_channel(channel: banjo_common::WlanChannel) -> Self {
            Self {
                valid_fields: banjo_wlan_associnfo::WlanRxInfoValid::CHAN_WIDTH.0
                    | banjo_wlan_associnfo::WlanRxInfoValid::RSSI.0
                    | banjo_wlan_associnfo::WlanRxInfoValid::SNR.0,
                channel,
                rssi_dbm: -40,
                snr_dbh: 35,

                // Default to 0 for these fields since there are no
                // other reasonable values to mock.
                rx_flags: banjo_wlan_softmac::WlanRxInfoFlags(0),
                phy: banjo_common::WlanPhyType::DSSS,
                data_rate: 0,
                mcs: 0,
            }
        }
    }

    impl From<MockWlanRxInfo> for banjo_wlan_softmac::WlanRxInfo {
        fn from(mock_rx_info: MockWlanRxInfo) -> banjo_wlan_softmac::WlanRxInfo {
            banjo_wlan_softmac::WlanRxInfo {
                rx_flags: mock_rx_info.rx_flags,
                valid_fields: mock_rx_info.valid_fields,
                phy: mock_rx_info.phy,
                data_rate: mock_rx_info.data_rate,
                channel: mock_rx_info.channel,
                mcs: mock_rx_info.mcs,
                rssi_dbm: mock_rx_info.rssi_dbm,
                snr_dbh: mock_rx_info.snr_dbh,
            }
        }
    }

    pub(crate) fn fake_control_handle(
        // We use this unused parameter to ensure that an executor exists.
        _exec: &fuchsia_async::TestExecutor,
    ) -> (fidl_mlme::MlmeControlHandle, fuchsia_zircon::Channel) {
        let (c1, c2) = fuchsia_zircon::Channel::create().unwrap();
        let async_c1 = fidl::AsyncChannel::from_channel(c1).unwrap();
        let request_stream = fidl_mlme::MlmeRequestStream::from_channel(async_c1);
        let control_handle = request_stream.control_handle();
        (control_handle, c2)
    }

    pub(crate) fn fake_key(address: [u8; 6]) -> fidl_mlme::SetKeyDescriptor {
        fidl_mlme::SetKeyDescriptor {
            cipher_suite_oui: [1, 2, 3],
            cipher_suite_type: 4,
            key_type: fidl_mlme::KeyType::Pairwise,
            address,
            key_id: 6,
            key: vec![1, 2, 3, 4, 5, 6, 7],
            rsc: 8,
        }
    }

    pub(crate) fn fake_mlme_set_keys_req(
        exec: &fasync::TestExecutor,
        address: [u8; 6],
    ) -> fidl_mlme::MlmeRequest {
        let (control_handle, _) = fake_control_handle(&exec);
        fidl_mlme::MlmeRequest::SetKeysReq {
            req: fidl_mlme::SetKeysRequest { keylist: vec![fake_key(address)] },
            control_handle,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{buffer::FakeBufferProvider, device::FakeDevice, test_utils::FakeMlme, *},
        fuchsia_async::TestExecutor,
        futures::channel::{mpsc, oneshot},
        std::task::Poll,
        wlan_common::assert_variant,
    };

    #[test]
    fn start_and_stop_main_loop() {
        let mut exec = TestExecutor::new().expect("Failed to create test executor");
        let mut device = FakeDevice::new(&exec);
        let buf_provider = FakeBufferProvider::new();
        let (sink, stream) = mpsc::unbounded();
        let (startup_sender, mut startup_receiver) = oneshot::channel();
        let mut main_loop = Box::pin(mlme_main_loop::<FakeMlme>(
            (),
            device.as_raw_device(),
            buf_provider,
            sink.clone(),
            stream,
            startup_sender,
        ));
        assert_variant!(exec.run_until_stalled(&mut main_loop), Poll::Pending);
        assert_variant!(startup_receiver.try_recv(), Ok(Some(Ok(()))));

        sink.unbounded_send(DriverEvent::Stop).expect("Failed to send stop event");
        assert_variant!(exec.run_until_stalled(&mut main_loop), Poll::Ready(()));
        assert!(sink.is_closed());
    }
}
