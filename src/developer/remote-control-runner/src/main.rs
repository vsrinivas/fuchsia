// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

async fn send_request(proxy: RemoteControlProxy) -> Result<(), Error> {
    // We just need to make a request to the RCS - it doesn't really matter
    // what we choose here so long as there are no side effects.
    let result = proxy.identify_host().await?;
    match result {
        Ok(_) => {
            println!("Successfully connected to RCS.");
            Ok(())
        }
        // If IdentifyHost fails internally, we don't really care
        // since we at least know that the RCS is now running.
        Err(_) => Ok(()),
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let rcs_proxy = connect_to_service::<RemoteControlMarker>().unwrap();
    send_request(rcs_proxy).await
}

#[cfg(test)]
mod test {
    use {
        crate::send_request,
        anyhow::Error,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_developer_remotecontrol::{
            IdentifyHostResponse, RemoteControlMarker, RemoteControlProxy, RemoteControlRequest,
        },
        fuchsia_async as fasync,
        futures::prelude::*,
    };

    fn setup_fake_rcs(handle_stream: bool) -> RemoteControlProxy {
        let (proxy, mut stream) = create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        if !handle_stream {
            return proxy;
        }

        fasync::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(RemoteControlRequest::IdentifyHost { responder }) => {
                        let _ = responder
                            .send(&mut Ok(IdentifyHostResponse { addresses: Some(vec![]) }))
                            .unwrap();
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_handles_successful_response() -> Result<(), Error> {
        let rcs_proxy = setup_fake_rcs(true);
        assert!(send_request(rcs_proxy).await.is_ok());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_handles_failed_response() -> Result<(), Error> {
        let rcs_proxy = setup_fake_rcs(false);
        assert!(send_request(rcs_proxy).await.is_err());
        Ok(())
    }
}
