// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::QmiClient,
    crate::errors::QmuxError,
    crate::transport::QmiTransport,
    failure::Error,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_telephony_ril::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, macros::*},
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::{
        future::Future,
        task::{AtomicWaker, Context, Poll},
        StreamExt, TryFutureExt, TryStreamExt,
    },
    qmi_protocol::QmiResult,
    qmi_protocol::*,
    std::pin::Pin,
    std::sync::Arc,
};

mod client;
mod errors;
mod transport;

type QmiModemPtr = Arc<Mutex<QmiModem>>;

struct QmiTransportMgr {
    transport: Option<Arc<QmiTransport>>,
    waker: AtomicWaker,
}

impl QmiTransportMgr {
    pub fn new() -> QmiTransportMgr {
        QmiTransportMgr { transport: None, waker: AtomicWaker::new() }
    }

    pub fn set_transport(&mut self, transport: QmiTransport) {
        self.transport = Some(Arc::new(transport));
        self.waker.wake();
    }
}

impl Future for &QmiTransportMgr {
    type Output = Arc<QmiTransport>;

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        match self.transport {
            Some(ref transport) => Poll::Ready(transport.clone()),
            None => {
                self.waker.register(cx.waker());
                Poll::Pending
            }
        }
    }
}

pub struct QmiModem {
    inner: QmiTransportMgr,
}

impl QmiModem {
    pub fn new() -> Self {
        QmiModem { inner: QmiTransportMgr::new() }
    }

    pub fn connect_transport(&mut self, chan: zx::Channel) -> bool {
        fx_log_info!("Connecting the transport");
        match fasync::Channel::from_channel(chan) {
            Ok(chan) => {
                if chan.is_closed() {
                    fx_log_err!("The transport channel is not open");
                    return false;
                }
                self.inner.set_transport(QmiTransport::new(chan));
                true
            }
            Err(_) => {
                fx_log_err!("Failed to convert a zircon channel to a fasync one");
                false
            }
        }
    }

    pub async fn create_client(&self) -> QmiClient {
        let transport = (&self.inner).await;
        let client = QmiClient::new(transport);
        client
    }
}

/// Craft a QMI Query given a handle and a client connection. Handles common error paths.
/// For more specialized interactions with the modem, prefer to call `client.send_msg()` directly.
macro_rules! qmi_query {
    ($responder:expr, $client:expr, $query:expr) => {{
        let resp: Result<QmiResult<_>, QmuxError> = $client.send_msg($query).await;
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
    }};
}

struct FrilService;
impl FrilService {
    pub fn spawn(modem: QmiModemPtr, stream: RadioInterfaceLayerRequestStream) {
        let server = async move {
            let client = {
                let modem_lock = modem.lock().await;
                Arc::new(modem_lock.create_client().await)
            };
            stream
                .try_for_each(move |req| Self::handle_request(client.clone(), req))
                .unwrap_or_else(|e| fx_log_err!("Error running {:?}", e))
                .await
        };
        fasync::spawn(server);
    }

    async fn handle_request(
        client: Arc<QmiClient>,
        request: RadioInterfaceLayerRequest,
    ) -> Result<(), fidl::Error> {
        match request {
            RadioInterfaceLayerRequest::GetSignalStrength { responder } => {
                let resp = qmi_query!(responder, client, NAS::GetSignalStrengthReq::new());
                if resp.radio_interface != 0x08 {
                    responder.send(&mut Err(RilError::UnsupportedNetworkType))?
                } else {
                    responder.send(&mut Ok(resp.signal_strength as f32))?
                }
            }
            RadioInterfaceLayerRequest::GetNetworkSettings { responder } => {
                let packet = qmi_query!(responder, client, WDS::GetCurrentSettingsReq::new(58160));
                responder.send(&mut Ok(NetworkSettings {
                    ip_v4_addr: packet.ipv4_addr.unwrap(),
                    ip_v4_dns: packet.ipv4_dns.unwrap(),
                    ip_v4_subnet: packet.ipv4_subnet.unwrap(),
                    ip_v4_gateway: packet.ipv4_gateway.unwrap(),
                    mtu: packet.mtu.unwrap(),
                }))?
            }
            RadioInterfaceLayerRequest::StartNetwork { apn, responder } => {
                let packet = qmi_query!(
                    responder,
                    client,
                    WDS::StartNetworkInterfaceReq::new(Some(apn), Some(4))
                );
                let (server_chan, client_chan) = zx::Channel::create().unwrap();
                let server_end = ServerEnd::<NetworkConnectionMarker>::new(server_chan.into());
                client
                    .set_data_connection(client::Connection {
                        pkt_handle: packet.packet_data_handle,
                        conn: server_end,
                    })
                    .await;
                let client_end = ClientEnd::<NetworkConnectionMarker>::new(client_chan.into());
                responder.send(&mut Ok(client_end))?
            }
            RadioInterfaceLayerRequest::GetDeviceIdentity { responder } => {
                let resp = qmi_query!(responder, client, DMS::GetDeviceSerialNumbersReq::new());
                responder.send(&mut Ok(resp.imei))?
            }
            RadioInterfaceLayerRequest::RadioPowerStatus { responder } => {
                let resp = qmi_query!(responder, client, DMS::GetOperatingModeReq::new());
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

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["ril-qmi"]).expect("Can't init logger");
    fx_log_info!("Starting ril-qmi...");

    let modem = Arc::new(Mutex::new(QmiModem::new()));
    let modem_setup = modem.clone();

    let mut fs = ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(move |stream| {
            fx_log_info!("New client connecting to the Fuchsia RIL");
            FrilService::spawn(modem.clone(), stream)
        })
        .add_fidl_service(move |mut stream: SetupRequestStream| {
            let modem = modem_setup.clone();
            fasync::spawn(async move {
                let res = stream.next().await.unwrap();
                if let Ok(SetupRequest::ConnectTransport { channel, responder }) = res {
                    let mut lock = modem.lock().await;
                    let status = lock.connect_transport(channel);
                    fx_log_info!("Connecting the service to the transport driver: {}", status);
                    if status {
                        let _ = responder.send(&mut Ok(()));
                    } else {
                        let _ = responder.send(&mut Err(RilError::TransportError));
                    }
                }
            });
        });

    fs.take_and_serve_directory_handle()?;

    Ok(fs.collect::<()>().await)
}
