// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

use {
    crate::client::QmiClient,
    crate::errors::QmuxError,
    crate::transport::QmiTransport,
    failure::{Error, ResultExt},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_telephony_ril::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, macros::*},
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::{StreamExt, TryFutureExt, TryStreamExt},
    qmi_protocol::QmiResult,
    qmi_protocol::*,
    std::sync::Arc,
};

mod client;
mod errors;
mod transport;

type QmiModemPtr = Arc<Mutex<QmiModem>>;

pub struct QmiModem {
    inner: Option<Arc<QmiTransport>>,
}

impl QmiModem {
    pub fn new() -> Self {
        QmiModem { inner: None }
    }

    pub fn new_with_transport(transport: Arc<QmiTransport>) -> Self {
        QmiModem { inner: Some(transport) }
    }

    pub fn connected(&self) -> bool {
        // TODO add additional logic for checking transport_channel open
        self.inner.is_some()
    }

    pub fn connect_transport(&mut self, chan: zx::Channel) -> bool {
        fx_log_info!("Connecting the transport");
        if self.connected() {
            fx_log_err!("Attempted to connect more than one transport");
            return false;
        }
        match fasync::Channel::from_channel(chan) {
            Ok(chan) => {
                if chan.is_closed() {
                    fx_log_err!("The transport channel is not open");
                    return false;
                }
                self.inner = Some(Arc::new(QmiTransport::new(chan)));
                true
            }
            Err(_) => {
                fx_log_err!("Failed to convert a zircon channel to a fasync one");
                false
            }
        }
    }

    pub async fn create_client(&self) -> Result<QmiClient, Error> {
        fx_log_info!("Client connecting...");
        if let Some(ref inner) = self.inner {
            let transport_inner = inner.clone();
            let client = QmiClient::new(transport_inner);
            Ok(client)
        } else {
            Err(QmuxError::NoTransport.into())
        }
    }
}

type ClientPtr = Arc<Mutex<Option<QmiClient>>>;

/// needs to be called before all requests that use a client
async fn setup_client<'a>(modem: QmiModemPtr, client_ptr: ClientPtr) {
    let mut client_lock = await!(client_ptr.lock());
    if client_lock.is_none() {
        let modem_lock = await!(modem.lock());
        match await!(modem_lock.create_client()) {
            Ok(alloced_client) => *client_lock = Some(alloced_client),
            Err(e) => {
                fx_log_err!("Failed to allocated a client: {}", e);
            }
        }
    };
}

/// Craft a QMI Query given a handle and a client connection. Handles common error paths.
/// For more specialized interactions with the modem, prefer to call `client.send_msg()` directly.
macro_rules! qmi_query {
    ($responder:expr, $client:expr, $query:expr) => {{
        match *await!($client.lock()) {
            Some(ref mut client) => {
                let resp: Result<QmiResult<_>, QmuxError> = await!(client.send_msg($query));
                match resp {
                    Ok(qmi_result) => {
                        match qmi_result {
                            Ok(qmi) => qmi,
                            Err(e) => {
                                fx_log_err!("Unknown Error: {:?}", e);
                                // TODO(bwb): Define conversion trait between errors and RIL errors
                                return $responder.send(&mut Err(RilError::UnknownError));
                            }
                        }
                    }
                    Err(e) => {
                        fx_log_err!("Transport Error: {}", e);
                        return $responder.send(&mut Err(RilError::TransportError));
                    }
                }
            }
            None => {
                return $responder.send(&mut Err(RilError::NoRadio));
            }
        }
    }};
}

struct FrilService;
impl FrilService {
    pub fn spawn(modem: QmiModemPtr, stream: RadioInterfaceLayerRequestStream) {
        let client = Arc::new(Mutex::new(None));
        let server = stream
            .try_for_each(move |req| Self::handle_request(modem.clone(), client.clone(), req))
            .unwrap_or_else(|e| fx_log_err!("Error running {:?}", e));
        fasync::spawn(server);
    }

    async fn handle_request(
        modem: QmiModemPtr,
        client: ClientPtr,
        request: RadioInterfaceLayerRequest,
    ) -> Result<(), fidl::Error> {
        // TODO(bwb) after component model v2, switch to on channel setup and
        // deprecated ConnectTransport method
        match request {
            RadioInterfaceLayerRequest::ConnectTransport { .. } => (), // does not need a client setup
            _ => await!(setup_client(modem.clone(), client.clone())),
        }

        match request {
            RadioInterfaceLayerRequest::ConnectTransport { channel, responder } => {
                let mut lock = await!(modem.lock());
                let status = lock.connect_transport(channel);
                fx_log_info!("Connecting the service to the transport driver: {}", status);
                if status {
                    return responder.send(&mut Ok(()));
                }
                responder.send(&mut Err(RilError::TransportError))?
            }
            RadioInterfaceLayerRequest::GetSignalStrength { responder } => {
                let resp: NAS::GetSignalStrengthResp =
                    qmi_query!(responder, client, NAS::GetSignalStrengthReq::new());
                if resp.radio_interface != 0x08 {
                    responder.send(&mut Err(RilError::UnsupportedNetworkType))?
                } else {
                    responder.send(&mut Ok(resp.signal_strength as f32))?
                }
            }
            RadioInterfaceLayerRequest::GetNetworkSettings { responder } => {
                let packet: WDS::GetCurrentSettingsResp =
                    qmi_query!(responder, client, WDS::GetCurrentSettingsReq::new(58160));
                responder.send(&mut Ok(NetworkSettings {
                        ip_v4_addr: packet.ipv4_addr.unwrap(),
                        ip_v4_dns: packet.ipv4_dns.unwrap(),
                        ip_v4_subnet: packet.ipv4_subnet.unwrap(),
                        ip_v4_gateway: packet.ipv4_gateway.unwrap(),
                        mtu: packet.mtu.unwrap(),
                    }))?
            }
            RadioInterfaceLayerRequest::StartNetwork { apn, responder } => {
                let packet: WDS::StartNetworkInterfaceResp = qmi_query!(
                    responder,
                    client,
                    WDS::StartNetworkInterfaceReq::new(Some(apn), Some(4))
                );
                let (server_chan, client_chan) = zx::Channel::create().unwrap();
                let server_end = ServerEnd::<NetworkConnectionMarker>::new(server_chan.into());
                if let Some(ref mut client) = *await!(client.lock()) {
                    client.data_conn = Some(client::Connection {
                        pkt_handle: packet.packet_data_handle,
                        conn: server_end,
                    });
                }
                let client_end = ClientEnd::<NetworkConnectionMarker>::new(client_chan.into());
                responder.send(&mut Ok(client_end))?
            }
            RadioInterfaceLayerRequest::GetDeviceIdentity { responder } => {
                let resp: DMS::GetDeviceSerialNumbersResp =
                    qmi_query!(responder, client, DMS::GetDeviceSerialNumbersReq::new());
                responder.send(&mut Ok(resp.imei))?
            }
            RadioInterfaceLayerRequest::RadioPowerStatus { responder } => {
                let resp: DMS::GetOperatingModeResp =
                    qmi_query!(responder, client, DMS::GetOperatingModeReq::new());
                if resp.operating_mode == 0x00 {
                    responder.send(&mut Ok(RadioPowerState::On))?
                } else {
                    responder.send(&mut Ok(RadioPowerState::Off))?
                }
            }
        }
        Ok(())
    }
}

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["ril-qmi"]).expect("Can't init logger");
    fx_log_info!("Starting ril-qmi...");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let modem = Arc::new(Mutex::new(QmiModem::new()));

    let mut fs = ServiceFs::new_local();
    fs.dir("public").add_fidl_service(move |stream| {
        fx_log_info!("New client connecting to the Fuchsia RIL");
        FrilService::spawn(modem.clone(), stream)
    });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
