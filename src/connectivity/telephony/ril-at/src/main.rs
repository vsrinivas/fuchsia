// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::transport::AtTransport;
use anyhow::Error;
use fidl_fuchsia_telephony_ril::*;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{self as syslog, macros::*};
use fuchsia_zircon as zx;
use futures::{
    future::Future,
    lock::Mutex,
    task::{AtomicWaker, Context},
    StreamExt, TryFutureExt, TryStreamExt,
};
use std::{pin::Pin, sync::Arc, task::Poll};

mod transport;

pub struct AtModem {
    transport: Option<AtTransport>,
    waker: AtomicWaker,
}

impl AtModem {
    pub fn new() -> Self {
        AtModem { transport: None, waker: AtomicWaker::new() }
    }

    async fn fidl_service_async(modem: Arc<Mutex<AtModem>>, mut stream: SetupRequestStream) -> () {
        let mut modem = modem.lock().await;
        let res = stream.next().await;
        if let Some(Ok(SetupRequest::ConnectTransport { channel, responder })) = res {
            let status = modem.connect_transport(channel);
            fx_log_info!("Connecting the service to the transport driver: {}", status);
            if status {
                let _result = responder.send(&mut Ok(()));
            } else {
                let _result = responder.send(&mut Err(RilError::TransportError));
            }
        }
    }

    pub fn fidl_service(modem: Arc<Mutex<AtModem>>) -> impl Fn(SetupRequestStream) -> () {
        move |stream| fasync::spawn(AtModem::fidl_service_async(modem.clone(), stream))
    }

    fn connect_transport(&mut self, chan: zx::Channel) -> bool {
        fx_log_info!("Connecting the transport");
        match fasync::Channel::from_channel(chan) {
            Ok(chan) => {
                if chan.is_closed() {
                    fx_log_err!("The transport channel is not open");
                    return false;
                }
                self.set_transport(AtTransport::new(chan));
                true
            }
            Err(_) => {
                fx_log_err!("Failed to convert a zircon channel to a fasync one");
                false
            }
        }
    }

    fn set_transport(&mut self, transport: AtTransport) {
        self.transport = Some(transport);
        self.waker.wake();
    }
}

//Wait for transport setup.
impl<'modem> Future for &AtModem {
    type Output = ();

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        match self.transport {
            Some(_) => Poll::Ready(()),
            None => {
                self.waker.register(cx.waker());
                Poll::Pending
            }
        }
    }
}

/// Craft a AT Query given a handle and a client connection. Handles common error paths.
/// For more specialized interactions with the modem, prefer to call `client.send_msg()` directly.
macro_rules! at_query {
    ($responder:expr, $transport:expr, $query:expr) => {{
        match $transport.send_msg($query).await {
            Ok(at) => at,
            Err(e) => {
                fx_log_err!("Transport Error: {:?}", e);
                return $responder.send(&mut Err(RilError::TransportError));
            }
        }
    }};
}

struct FrilService;
impl FrilService {
    pub fn fidl_service(
        modem: Arc<Mutex<AtModem>>,
    ) -> impl Fn(RadioInterfaceLayerRequestStream) -> () {
        move |stream: RadioInterfaceLayerRequestStream| {
            fx_log_info!("New client connecting to the Fuchsia RIL");
            fasync::spawn(FrilService::fidl_service_async(modem.clone(), stream))
        }
    }

    async fn fidl_service_async(
        modem: Arc<Mutex<AtModem>>,
        stream: RadioInterfaceLayerRequestStream,
    ) {
        stream
            .try_for_each(move |req| Self::handle_request(modem.clone(), req))
            .unwrap_or_else(|e| fx_log_err!("Error running {:?}", e))
            .await
    }

    async fn handle_request(
        modem: Arc<Mutex<AtModem>>,
        request: RadioInterfaceLayerRequest,
    ) -> Result<(), fidl::Error> {
        let mut modem = modem.lock().await;
        (&(*modem)).await; // Await transport setup.
        // This await  is ok since the transport has been set up.
        let transport = (&mut modem.transport).as_mut().unwrap(); 
        match request {
            RadioInterfaceLayerRequest::RawCommand { command, responder } => {
                let resp = at_query!(responder, transport, command);

                responder.send(&mut Ok(resp))?;
            }
            // Translate requests here
            _ => (),
        }
        Ok(())
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["ril-at"]).expect("Can't init logger");
    fx_log_info!("Starting ril-at...");

    let modem = Arc::new(Mutex::new(AtModem::new()));

    let mut fs = ServiceFs::new_local();
    let mut dir = fs.dir("svc");

    // Add connection to upstream clients.
    dir.add_fidl_service(FrilService::fidl_service(modem.clone()));
    // Add connection to downstream driver.
    dir.add_fidl_service(AtModem::fidl_service(modem.clone()));

    fs.take_and_serve_directory_handle()?;
    Ok(fs.collect::<()>().await)
}
