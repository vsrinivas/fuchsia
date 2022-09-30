// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bridge::{Bridge, OptOutPreference};
use anyhow::anyhow;
use fidl_fuchsia_update_config::{
    OptOutAdminError, OptOutAdminRequest, OptOutAdminRequestStream, OptOutRequest,
    OptOutRequestStream,
};
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use futures::{channel::mpsc, prelude::*};
use tracing::warn;

/// ServiceFs, configured for single-threaded execution and handling services listed in
/// [`IncomingServices`].
pub type Fs = ServiceFs<ServiceObjLocal<'static, IncomingService>>;

/// FIDL services served by this component.
pub enum IncomingService {
    /// Read-only opt-out protocol.
    OptOut(OptOutRequestStream),

    /// Write-only opt-out protocol.
    OptOutAdmin(OptOutAdminRequestStream),
}

enum IncomingRequest {
    OptOut(OptOutRequest),
    OptOutAdmin(OptOutAdminRequest),
}

/// Register the exported FIDL services and serve incoming connections.
pub async fn serve(mut fs: Fs, storage: &mut dyn Bridge) {
    fs.dir("svc")
        .add_fidl_service(IncomingService::OptOut)
        .add_fidl_service(IncomingService::OptOutAdmin);

    serve_connections(fs, storage).await
}

async fn handle_request(req: IncomingRequest, storage: &mut dyn Bridge) {
    match req {
        IncomingRequest::OptOut(OptOutRequest::Get { responder }) => {
            let res = match storage.get_opt_out().await {
                Ok(value) => value.into(),
                Err(e) => {
                    warn!(
                        "Could not determine opt-out status, closing the request channel: {:#}",
                        anyhow!(e)
                    );
                    return;
                }
            };

            if let Err(e) = responder.send(res) {
                warn!("Could not respond to OptOut::Get request: {:#}", anyhow!(e));
            }
        }
        IncomingRequest::OptOutAdmin(OptOutAdminRequest::Set { value, responder }) => {
            let mut res = match storage.set_opt_out(value.into()).await {
                Ok(()) => Ok(()),
                Err(_) => Err(OptOutAdminError::Internal),
            };

            if let Err(e) = responder.send(&mut res) {
                warn!("Could not respond to OptOut::Set request: {:#}", anyhow!(e));
            }
        }
    }
}

/// Serve incoming connections using the provided backing `storage`.
async fn serve_connections(
    connections: impl Stream<Item = IncomingService> + 'static,
    storage: &mut dyn Bridge,
) {
    let (send_requests, mut recv_requests) = mpsc::channel(1);

    // Demux N FIDL connections into a single request stream.
    let forward_requests =
        fuchsia_async::Task::local(connections.for_each_concurrent(None, move |conn| {
            let send_requests = send_requests.clone().sink_map_err(SinkError::Forward);
            async move {
                let res = match conn {
                    IncomingService::OptOut(conn) => {
                        conn.map_ok(IncomingRequest::OptOut)
                            .map_err(SinkError::Read)
                            .forward(send_requests)
                            .await
                    }
                    IncomingService::OptOutAdmin(conn) => {
                        conn.map_ok(IncomingRequest::OptOutAdmin)
                            .map_err(SinkError::Read)
                            .forward(send_requests)
                            .await
                    }
                };
                match res {
                    Ok(()) => {}
                    Err(e @ SinkError::Read(_)) => {
                        warn!("Closing request channel: {:#}", anyhow!(e))
                    }
                    Err(SinkError::Forward(_)) => {
                        // unreachable. The receive side is only closed after this task finishes.
                    }
                }
            }
        }));

    // Serve that single request stream 1 request at a time.
    while let Some(request) = recv_requests.next().await {
        handle_request(request, storage).await;
    }

    forward_requests.await
}

/// An error encountered while reading a fidl request or forwarding it to the single stream of
/// requests.
#[derive(Debug, thiserror::Error)]
enum SinkError {
    #[error("while reading the request")]
    Read(#[source] fidl::Error),

    #[error("while forwarding the request to the handler")]
    Forward(#[source] mpsc::SendError),
}

impl From<fidl_fuchsia_update_config::OptOutPreference> for OptOutPreference {
    fn from(x: fidl_fuchsia_update_config::OptOutPreference) -> Self {
        use fidl_fuchsia_update_config::OptOutPreference::*;
        match x {
            AllowAllUpdates => OptOutPreference::AllowAllUpdates,
            AllowOnlySecurityUpdates => OptOutPreference::AllowOnlySecurityUpdates,
        }
    }
}

impl From<OptOutPreference> for fidl_fuchsia_update_config::OptOutPreference {
    fn from(x: OptOutPreference) -> Self {
        use fidl_fuchsia_update_config::OptOutPreference::*;
        match x {
            OptOutPreference::AllowAllUpdates => AllowAllUpdates,
            OptOutPreference::AllowOnlySecurityUpdates => AllowOnlySecurityUpdates,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bridge;
    use assert_matches::assert_matches;
    use fidl_fuchsia_update_config::{
        OptOutAdminError, OptOutAdminMarker, OptOutAdminProxy, OptOutMarker,
        OptOutPreference as FidlOptOutPreference, OptOutProxy,
    };
    use fuchsia_async::Task;
    use futures::channel::mpsc;

    fn spawn_serve(mut storage: impl Bridge + 'static) -> (Connector, Task<()>) {
        let (send, recv) = mpsc::unbounded();

        let svc = async move { serve_connections(recv, &mut storage).await };

        (Connector(send), Task::local(svc))
    }

    struct Connector(mpsc::UnboundedSender<IncomingService>);

    impl Connector {
        fn opt_out(&self) -> OptOutProxy {
            let (proxy, stream) =
                fidl::endpoints::create_proxy_and_stream::<OptOutMarker>().unwrap();
            self.0.unbounded_send(IncomingService::OptOut(stream)).unwrap();
            proxy
        }
        fn opt_out_admin(&self) -> OptOutAdminProxy {
            let (proxy, stream) =
                fidl::endpoints::create_proxy_and_stream::<OptOutAdminMarker>().unwrap();
            self.0.unbounded_send(IncomingService::OptOutAdmin(stream)).unwrap();
            proxy
        }
    }

    #[fuchsia::test]
    async fn start_stop() {
        let fs = ServiceFs::new_local();

        serve(fs, &mut bridge::testing::Error).await
    }

    #[fuchsia::test]
    async fn get_initial_state() {
        let (connector, svc) =
            spawn_serve(bridge::testing::Fake::new(OptOutPreference::AllowAllUpdates));
        assert_eq!(connector.opt_out().get().await.unwrap(), FidlOptOutPreference::AllowAllUpdates);
        drop(connector);
        svc.await;

        let (connector, svc) =
            spawn_serve(bridge::testing::Fake::new(OptOutPreference::AllowOnlySecurityUpdates));
        assert_eq!(
            connector.opt_out().get().await.unwrap(),
            FidlOptOutPreference::AllowOnlySecurityUpdates
        );
        drop(connector);
        svc.await;
    }

    #[fuchsia::test]
    async fn uses_storage_to_provide_preference() {
        let storage = bridge::testing::Fake::new(OptOutPreference::AllowAllUpdates);
        let (connector, svc) = spawn_serve(storage);

        let get1 = connector.opt_out();
        let get2 = connector.opt_out();
        let set1 = connector.opt_out_admin();

        assert_eq!(get1.get().await.unwrap(), FidlOptOutPreference::AllowAllUpdates);
        assert_eq!(get2.get().await.unwrap(), FidlOptOutPreference::AllowAllUpdates);

        assert_matches!(set1.set(FidlOptOutPreference::AllowOnlySecurityUpdates).await, Ok(Ok(())));

        assert_eq!(get1.get().await.unwrap(), FidlOptOutPreference::AllowOnlySecurityUpdates);
        assert_eq!(get2.get().await.unwrap(), FidlOptOutPreference::AllowOnlySecurityUpdates);

        assert_matches!(set1.set(FidlOptOutPreference::AllowAllUpdates).await, Ok(Ok(())));

        assert_eq!(get1.get().await.unwrap(), FidlOptOutPreference::AllowAllUpdates);
        assert_eq!(connector.opt_out().get().await.unwrap(), FidlOptOutPreference::AllowAllUpdates);

        // close proxy connections and our ability to open new ones, allowing svc to finish its
        // work and complete.
        drop(get1);
        drop(get2);
        drop(set1);
        drop(connector);
        svc.await;
    }

    #[fuchsia::test]
    async fn closes_connection_on_read_error() {
        let (storage, fail_requests) =
            bridge::testing::Fake::new_with_error_toggle(OptOutPreference::AllowAllUpdates);
        let (connector, svc) = spawn_serve(storage);

        let proxy1 = connector.opt_out();
        let proxy2 = connector.opt_out();

        // Make sure the connections are being served before we break things.
        assert_eq!(proxy1.get().await.unwrap(), FidlOptOutPreference::AllowAllUpdates);
        assert_eq!(proxy2.get().await.unwrap(), FidlOptOutPreference::AllowAllUpdates);

        fail_requests.set(true);
        assert_matches!(proxy1.get().await, Err(_));

        // The proxy that observed should be closed, and the proxy that did not should still be
        // open.
        fail_requests.set(false);
        assert_matches!(proxy1.get().await, Err(_));
        assert_eq!(proxy2.get().await.unwrap(), FidlOptOutPreference::AllowAllUpdates);

        // close proxy connections and our ability to open new ones, allowing svc to finish its
        // work and complete.
        drop(proxy2);

        drop(connector);
        svc.await;
    }

    #[fuchsia::test]
    async fn responds_with_error_on_write_error() {
        let (storage, fail_requests) =
            bridge::testing::Fake::new_with_error_toggle(OptOutPreference::AllowAllUpdates);
        let (connector, svc) = spawn_serve(storage);

        let proxy = connector.opt_out_admin();

        fail_requests.set(true);
        assert_matches!(
            proxy.set(FidlOptOutPreference::AllowAllUpdates).await,
            Ok(Err(OptOutAdminError::Internal))
        );

        // The channel should still be open.
        fail_requests.set(false);
        assert_matches!(proxy.set(FidlOptOutPreference::AllowAllUpdates).await, Ok(Ok(())));

        // close proxy connections and our ability to open new ones, allowing svc to finish its
        // work and complete.
        drop(proxy);
        drop(connector);
        svc.await;
    }
}
