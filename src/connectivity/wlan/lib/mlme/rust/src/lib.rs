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
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::{mpsc, oneshot},
        select, StreamExt,
    },
    log::{error, info, warn},
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

// We support a fake MLME internal representation that allows tests written in C++ to manually
// tweak the system time.
// TODO(fxbug.dev/45464): Remove when tests are all in Rust.
enum MlmeHandleInternal {
    Real {
        join_handle: std::thread::JoinHandle<()>,
    },
    Fake {
        executor: fasync::TestExecutor,
        future: std::pin::Pin<Box<dyn futures::Future<Output = ()>>>,
    },
}

/// MlmeHandle is the only access we have to our MLME after spinning it off into its own
/// event loop thread.
pub struct MlmeHandle {
    driver_event_sink: mpsc::UnboundedSender<DriverEvent>,
    internal: Option<MlmeHandleInternal>,
}

impl MlmeHandle {
    pub fn stop(&mut self) {
        if let Err(e) = self.driver_event_sink.unbounded_send(DriverEvent::Stop) {
            error!("Cannot signal MLME event loop thread: {}", e);
        }
        match self.internal.take() {
            Some(MlmeHandleInternal::Real { join_handle }) => {
                // This unwrap will only fail if the thread panics.
                if let Err(e) = join_handle.join() {
                    error!("MLME event loop thread panicked: {:?}", e);
                }
            }
            Some(MlmeHandleInternal::Fake { mut executor, mut future }) => {
                // Verify that our main thread would exit now.
                assert!(executor.run_until_stalled(&mut future.as_mut()).is_ready());
            }
            None => warn!("Called stop on already stopped MLME"),
        }
    }

    pub fn delete(mut self) {
        if self.internal.is_some() {
            warn!("Called delete on MlmeHandle before calling stop.");
            self.stop()
        }
    }

    pub fn queue_eth_frame_tx(&mut self, bytes: Vec<u8>) -> Result<(), Error> {
        self.driver_event_sink
            .unbounded_send(DriverEvent::EthFrameTx { bytes: bytes.into() })
            .map_err(|e| e.into())
    }

    // Fns used to interact with an MLME running in test mode.
    // TODO(fxbug.dev/45464): Remove when tests are all in Rust.

    pub fn advance_fake_time(&mut self, nanos: i64) {
        match &mut self.internal {
            Some(MlmeHandleInternal::Real { .. }) => {
                panic!("Called advance_fake_time on a real MLME")
            }
            Some(MlmeHandleInternal::Fake { executor, future }) => {
                let time = executor.now();
                executor.set_fake_time(time + fasync::Duration::from_nanos(nanos));
                executor.wake_expired_timers();
                let _ = executor.run_until_stalled(&mut future.as_mut());
            }
            None => panic!("Called advance_fake_time on stopped MLME"),
        }
    }

    pub fn run_until_stalled(&mut self) {
        match &mut self.internal {
            Some(MlmeHandleInternal::Real { .. }) => {
                panic!("Called run_until_stalled on a real MLME")
            }
            Some(MlmeHandleInternal::Fake { executor, future }) => {
                let _ = executor.run_until_stalled(&mut future.as_mut());
            }
            None => panic!("Called run_until_stalled on stopped MLME"),
        }
    }
}

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

pub struct Mlme<T: MlmeImpl> {
    mlme_impl: T,
    minstrel: Option<MinstrelWrapper>,
    // A stream of requests coming from the parent SME of this MLME.
    mlme_request_stream: fidl_mlme::MlmeRequestStream,
    // A stream of events initiated by C++ device drivers and then buffered here
    // by our MlmeHandle.
    driver_event_stream: mpsc::UnboundedReceiver<DriverEvent>,
    time_stream: common::timer::TimeStream<T::TimerEvent>,
    minstrel_time_stream: common::timer::TimeStream<()>,
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

// Require a static lifetime so we can move this MLME into an event loop task.
impl<T: 'static + MlmeImpl> Mlme<T> {
    pub fn start(
        config: T::Config,
        device: DeviceInterface,
        buf_provider: buffer::BufferProvider,
    ) -> Result<MlmeHandle, Error> {
        let (driver_event_sink, driver_event_stream) = mpsc::unbounded();
        // This sink is used both by the inderlying iface to forward up driver events, as well
        // as via the MlmeHandle to send ethernet frames and terminate MLME.
        let driver_event_sink_clone = driver_event_sink.clone();

        let (startup_sender, startup_receiver) = oneshot::channel();

        // Everything else happens in a new thread so that we can switch into an async context
        // without requiring all parts of MLME to impl Send.
        let join_handle = std::thread::spawn(move || {
            info!("Starting WLAN MLME main loop");
            let mut executor = fasync::LocalExecutor::new().unwrap();
            let future = Self::main_loop_thread(
                config,
                device,
                buf_provider,
                driver_event_sink_clone,
                driver_event_stream,
                startup_sender,
            );
            executor.run_singlethreaded(future);
        });

        let mut executor = fasync::LocalExecutor::new().unwrap();
        let startup_result = executor.run_singlethreaded(startup_receiver);
        match startup_result.map_err(|e| Error::from(e)) {
            Ok(Ok(())) => Ok(MlmeHandle {
                driver_event_sink,
                internal: Some(MlmeHandleInternal::Real { join_handle }),
            }),
            Err(err) | Ok(Err(err)) => match join_handle.join() {
                Ok(()) => bail!("Failed to start the MLME event loop: {:?}", err),
                Err(panic_err) => {
                    bail!("MLME event loop failed and then panicked: {}, {:?}", err, panic_err)
                }
            },
        }
    }

    // Create an MLME in a test configuration. This MLME will never do anything unless it's progressed
    // using MlmeHandle::advance_fake_time and MlmeHandle::run_until_stalled.
    pub fn start_test(
        config: T::Config,
        device: DeviceInterface,
        buf_provider: buffer::BufferProvider,
    ) -> MlmeHandle {
        let executor = fasync::TestExecutor::new_with_fake_time().unwrap();
        Self::start_test_with_executor(config, device, buf_provider, executor)
    }

    pub fn start_test_with_executor(
        config: T::Config,
        device: DeviceInterface,
        buf_provider: buffer::BufferProvider,
        mut executor: fasync::TestExecutor,
    ) -> MlmeHandle {
        let (driver_event_sink, driver_event_stream) = mpsc::unbounded();
        let driver_event_sink_clone = driver_event_sink.clone();
        let (startup_sender, mut startup_receiver) = oneshot::channel();
        let mut future = Box::pin(Self::main_loop_thread(
            config,
            device,
            buf_provider,
            driver_event_sink_clone,
            driver_event_stream,
            startup_sender,
        ));
        let _ = executor.run_until_stalled(&mut future.as_mut());
        startup_receiver
            .try_recv()
            .unwrap()
            .expect("Test MLME setup stalled.")
            .expect("Test MLME setup failed.");

        MlmeHandle {
            driver_event_sink,
            internal: Some(MlmeHandleInternal::Fake { executor, future }),
        }
    }

    async fn main_loop_thread(
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
                startup_sender
                    .send(Err(anyhow!("Failed to get MLME request stream: {}", e)))
                    .unwrap();
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

        let mlme = Self {
            mlme_impl,
            minstrel,
            mlme_request_stream,
            driver_event_stream,
            time_stream,
            minstrel_time_stream,
        };

        // Startup is complete. Signal the main thread to proceed.
        // Failure to unwrap indicates a critical failure in the driver init thread.
        startup_sender.send(Ok(())).unwrap();

        let result = Self::run_main_loop(mlme).await;
        match result {
            Ok(()) => info!("MLME event loop exited gracefully."),
            Err(e) => error!("MLME event loop exited with error: {:?}", e),
        }
    }

    /// Begin processing MLME events.
    /// Does not return until iface destruction is requested via DriverEvent::Stop, unless
    /// a critical error occurs. Note that MlmeHandle::stop will work in either case.
    pub async fn run_main_loop(mut self) -> Result<(), Error> {
        let mut timer_stream =
            common::timer::make_async_timed_event_stream(self.time_stream).fuse();
        let mut minstrel_timer_stream =
            common::timer::make_async_timed_event_stream(self.minstrel_time_stream).fuse();
        loop {
            select! {
                // Process requests from SME.
                mlme_request = self.mlme_request_stream.next() => match mlme_request {
                    Some(req) => {
                        match req {
                            Ok(req) => {
                                let method_name = req.method_name();
                                if let Err(e) = self.mlme_impl.handle_mlme_message(req) {
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
                driver_event = self.driver_event_stream.next() => match driver_event {
                    Some(event) => match event {
                        // DriverEvent::Stop indicates a safe shutdown.
                        DriverEvent::Stop => return Ok(()),
                        DriverEvent::MacFrameRx { bytes, rx_info } => {
                            self.mlme_impl.handle_mac_frame_rx(&bytes[..], rx_info);
                        }
                        DriverEvent::EthFrameTx { bytes } => {
                            if let Err(e) = self.mlme_impl.handle_eth_frame_tx(&bytes[..]) {
                                // TODO(fxbug.dev/45464): Keep a counter of these failures.
                                info!("Failed to handle eth frame: {}", e);
                            }
                        }
                        DriverEvent::ScanComplete { status, scan_id } => {
                            self.mlme_impl.handle_scan_complete(status, scan_id)
                        },
                        DriverEvent::TxStatusReport { tx_status } => {
                            if let Some(minstrel) = self.minstrel.as_ref() {
                                minstrel.lock().handle_tx_status_report(&tx_status)
                            }
                        }
                        DriverEvent::Status { status } => {
                            self.mlme_impl.access_device().set_eth_status(status)
                        }
                    },
                    None => bail!("Driver event stream terminated unexpectedly."),
                },
                timed_event = timer_stream.select_next_some() => {
                    self.mlme_impl.handle_timeout(timed_event.id, timed_event.event);
                }
                _minstrel_timeout = minstrel_timer_stream.select_next_some() => {
                    if let Some(minstrel) = self.minstrel.as_ref() {
                        minstrel.lock().handle_timeout()
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod test_utils {
    use {
        super::*, banjo_fuchsia_hardware_wlan_associnfo as banjo_wlan_associnfo,
        banjo_fuchsia_wlan_common as banjo_common, fidl::endpoints::RequestStream,
        std::default::Default,
    };

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

    impl Default for MockWlanRxInfo {
        fn default() -> Self {
            Self {
                valid_fields: banjo_wlan_associnfo::WlanRxInfoValid::CHAN_WIDTH.0
                    | banjo_wlan_associnfo::WlanRxInfoValid::RSSI.0
                    | banjo_wlan_associnfo::WlanRxInfoValid::SNR.0,
                channel: banjo_common::WlanChannel {
                    primary: 1,
                    cbw: banjo_common::ChannelBandwidth::CBW20,
                    secondary80: 0,
                },
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
            _scheduler: common::timer::Timer<Self::TimerEvent>,
        ) -> Self {
            Self { device }
        }

        fn handle_mlme_message(&mut self, _msg: fidl_mlme::MlmeRequest) -> Result<(), Error> {
            unimplemented!()
        }

        fn handle_mac_frame_rx(&mut self, _bytes: &[u8], _rx_info: banjo_wlan_softmac::WlanRxInfo) {
            unimplemented!()
        }

        fn handle_eth_frame_tx(&mut self, _bytes: &[u8]) -> Result<(), Error> {
            unimplemented!()
        }

        fn handle_scan_complete(&mut self, _status: zx::Status, _scan_id: u64) {
            unimplemented!()
        }

        fn handle_timeout(&mut self, _event_id: common::timer::EventId, _event: Self::TimerEvent) {
            unimplemented!()
        }

        fn access_device(&mut self) -> &mut Device {
            &mut self.device
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::device::test_utils::FakeDevice};

    #[fuchsia::test]
    fn test_mlme_handle_use_after_stop() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let fake_config = ();
        let mut fake_device = FakeDevice::new(&mut exec);
        let fake_buffer_provider = buffer::FakeBufferProvider::new();
        let mut handle = Mlme::<test_utils::FakeMlme>::start_test_with_executor(
            fake_config,
            fake_device.as_raw_device(),
            fake_buffer_provider,
            exec,
        );

        handle.stop();
        handle
            .queue_eth_frame_tx(vec![0u8; 10])
            .expect_err("Shouldn't be able to queue tx after stopping MLME");
    }
}
