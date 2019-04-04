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
    fuchsia_component::server::ServiceFs,
    fuchsia_async as fasync,
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
        client_ptr: ClientPtr,
        request: RadioInterfaceLayerRequest,
    ) -> Result<(), fidl::Error> {
        // TODO(bwb) after component model v2, switch to on channel setup and
        // deprecated ConnectTransport method
        match request {
            RadioInterfaceLayerRequest::ConnectTransport { .. } => (), // does not need a client setup
            _ => await!(setup_client(modem.clone(), client_ptr.clone())),
        }

        match request {
            RadioInterfaceLayerRequest::ConnectTransport { channel, responder } => {
                let mut lock = await!(modem.lock());
                let status = lock.connect_transport(channel);
                fx_log_info!("Connecting the service to the transport driver: {}", status);
                return responder.send(status);
            }
            RadioInterfaceLayerRequest::GetNetworkSettings { responder } => {
                match *await!(client_ptr.lock()) {
                    Some(ref mut client) => {
                        // TODO find out how to structure this u32 in a readable way
                        let resp: QmiResult<WDS::GetCurrentSettingsResp> =
                            await!(client.send_msg(WDS::GetCurrentSettingsReq::new(58160)))
                                .unwrap();
                        match resp {
                            Ok(packet) => responder.send(
                                &mut GetNetworkSettingsReturn::Settings(NetworkSettings {
                                    ip_v4_addr: packet.ipv4_addr.unwrap(),
                                    ip_v4_dns: packet.ipv4_dns.unwrap(),
                                    ip_v4_subnet: packet.ipv4_subnet.unwrap(),
                                    ip_v4_gateway: packet.ipv4_gateway.unwrap(),
                                    mtu: packet.mtu.unwrap(),
                                }),
                            )?,
                            Err(e) => {
                                fx_log_err!("Error network: {:?}", e);
                                // TODO different error
                                responder
                                    .send(&mut GetNetworkSettingsReturn::Error(RilError::NoRadio))?
                            }
                        }
                    }
                    None => {
                        responder.send(&mut GetNetworkSettingsReturn::Error(RilError::NoRadio))?
                    }
                }
            }
            RadioInterfaceLayerRequest::StartNetwork { apn, responder } => {
                match *await!(client_ptr.lock()) {
                    Some(ref mut client) => {
                        let resp: QmiResult<WDS::StartNetworkInterfaceResp> =
                            await!(client
                                .send_msg(WDS::StartNetworkInterfaceReq::new(Some(apn), Some(4))))
                            .unwrap();
                        match resp {
                            Ok(packet) => {
                                let (server_chan, client_chan) = zx::Channel::create().unwrap();
                                let server_end =
                                    ServerEnd::<NetworkConnectionMarker>::new(server_chan.into());
                                client.data_conn = Some(client::Connection {
                                    pkt_handle: packet.packet_data_handle,
                                    conn: server_end,
                                });
                                let client_end =
                                    ClientEnd::<NetworkConnectionMarker>::new(client_chan.into());
                                responder.send(&mut StartNetworkReturn::Conn(client_end))?
                            }
                            Err(e) => {
                                fx_log_info!("error network: {:?}", e);
                                // TODO different error
                                responder.send(&mut StartNetworkReturn::Error(RilError::NoRadio))?
                            }
                        }
                    }
                    None => responder.send(&mut StartNetworkReturn::Error(RilError::NoRadio))?,
                }
            }
            RadioInterfaceLayerRequest::GetDeviceIdentity { responder } => {
                match *await!(client_ptr.lock()) {
                    Some(ref mut client) => {
                        let resp: QmiResult<DMS::GetDeviceSerialNumbersResp> =
                            await!(client.send_msg(DMS::GetDeviceSerialNumbersReq::new())).unwrap();
                        responder.send(&mut GetDeviceIdentityReturn::Imei(resp.unwrap().imei))?
                    }
                    None => {
                        responder.send(&mut GetDeviceIdentityReturn::Error(RilError::NoRadio))?
                    }
                }
            }
            RadioInterfaceLayerRequest::RadioPowerStatus { responder } => {
                match *await!(client_ptr.lock()) {
                    Some(ref mut client) => {
                        let resp: DMS::GetOperatingModeResp =
                            await!(client.send_msg(DMS::GetOperatingModeReq::new()))
                                .unwrap()
                                .unwrap();
                        if resp.operating_mode == 0x00 {
                            responder
                                .send(&mut RadioPowerStatusReturn::Result(RadioPowerState::On))?
                        } else {
                            responder
                                .send(&mut RadioPowerStatusReturn::Result(RadioPowerState::Off))?
                        }
                    }
                    None => {
                        responder.send(&mut RadioPowerStatusReturn::Error(RilError::NoRadio))?
                    }
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
    fs.dir("public")
        .add_fidl_service(move |stream| {
            fx_log_info!("New client connecting to the Fuchsia RIL");
            FrilService::spawn(modem.clone(), stream)
        });
    fs.take_and_serve_directory_handle()?;

    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
