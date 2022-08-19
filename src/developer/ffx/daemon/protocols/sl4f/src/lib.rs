// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    async_net::TcpListener,
    async_trait::async_trait,
    ffx_config::{self},
    fidl_fuchsia_developer_ffx as ffx,
    fidl_fuchsia_sl4f_ffx::{Sl4fBridgeMarker, Sl4fBridgeProxy, Sl4fBridgeRequest},
    fuchsia_async::Task,
    fuchsia_repo::server::ConnectionStream,
    futures::FutureExt as _,
    futures::TryStreamExt,
    hyper::{
        service::{make_service_fn, service_fn},
        Body, Method, Response, StatusCode,
    },
    protocols::prelude::*,
    std::convert::Infallible,
    std::net::{IpAddr, Ipv6Addr, SocketAddr},
    std::str,
    std::sync::Arc,
};

// The selector that identifies the component running the bridge protocol on the
// device.
const SL4F_BRIDGE_SELECTOR: &str = "core/sl4f_bridge_server:expose:fuchsia.sl4f.ffx.Sl4fBridge";

// The host-side server port for the SL4F server running in the ffx daemon.
const SERVER_PORT: u16 = 8034;

#[ffx_protocol]
#[derive(Default)]
pub struct Sl4fBridge {
    server_task: Option<Task<()>>,
}

// Create a trait that encapsulates the proxy execution function for testing.
#[async_trait]
pub trait Bridge {
    async fn execute(&self, target_query: ffx::TargetQuery, req: &str) -> String;
    fn target(&self) -> Option<String>;
}

#[derive(Clone)]
struct BridgeProxy {
    proxy: Arc<Sl4fBridgeProxy>,
    target: Option<String>,
}

#[async_trait]
impl Bridge for BridgeProxy {
    async fn execute(&self, target_query: ffx::TargetQuery, req: &str) -> String {
        self.proxy.execute(target_query, req).await.unwrap()
    }
    fn target(&self) -> Option<String> {
        self.target.clone()
    }
}

/// Routes incoming requests to host-side SL4F server.
pub async fn route_request<B: Bridge>(
    bridge: B,
    request: hyper::Request<hyper::Body>,
) -> hyper::Response<hyper::Body> {
    match (request.method(), request.uri().path()) {
        (&Method::POST, "/") => {
            let bytes = hyper::body::to_bytes(request.into_body()).await.unwrap();
            let req = str::from_utf8(&bytes).expect("response was not valid utf-8");
            let target_query =
                ffx::TargetQuery { string_matcher: bridge.target(), ..ffx::TargetQuery::EMPTY };
            tracing::info!("  route_request() to {:?}", &target_query.string_matcher);
            let resp = bridge.execute(target_query, req).await;
            Response::new(Body::from(resp))
        }
        _ => Response::builder()
            .status(StatusCode::NOT_FOUND)
            .body(Body::from("unknown server request"))
            .unwrap(),
    }
}

// These commands handle the Sl4fBridge protocol requests that are sent to the ffx daemon.
#[async_trait(?Send)]
impl FidlProtocol for Sl4fBridge {
    type Protocol = Sl4fBridgeMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, _cx: &Context, _req: Sl4fBridgeRequest) -> Result<()> {
        Ok(())
    }

    // When the SL4F plugin is first called, it will run this function. Start the host-side server
    // that serves HTTP/JSON requests from the host-side clients.
    async fn start(&mut self, cx: &Context) -> Result<()> {
        let addr = SocketAddr::new(IpAddr::V6(Ipv6Addr::LOCALHOST), SERVER_PORT);
        let listener = TcpListener::bind(&addr).await?;
        tracing::info!("host-side SL4F proxy server listening on: {:?}", addr);

        let proxy = cx.open_target_proxy::<Sl4fBridgeMarker>(None, SL4F_BRIDGE_SELECTOR).await?;
        let proxy = Arc::new(proxy);
        let target: Option<String> =
            ffx_config::get("target.default").await.expect("couldn't read default target");
        let make_svc = make_service_fn(move |_: &ConnectionStream| {
            let proxy = BridgeProxy { proxy: proxy.clone(), target: target.clone() };
            futures::future::ok::<_, Infallible>(service_fn(move |request| {
                route_request(proxy.clone(), request).map(Ok::<_, Infallible>)
            }))
        });

        self.server_task.replace(Task::local(async move {
            let server = hyper::Server::builder(hyper::server::accept::from_stream(
                listener.incoming().map_ok(ConnectionStream::Tcp),
            ))
            .executor(fuchsia_hyper::Executor)
            .serve(make_svc);
            server.await.expect("SL4F host-side proxy server died");
        }));
        Ok(())
    }

    // This function will be called by the FFX daemon when it is stopping the protocol. Unused.
    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::Bridge,
        crate::route_request,
        async_trait::async_trait,
        fidl_fuchsia_developer_ffx as ffx,
        hyper::{Body, Request, StatusCode},
    };

    struct MockBridgeProxy<'a> {
        resp: &'a str,
    }

    #[async_trait]
    impl Bridge for MockBridgeProxy<'_> {
        async fn execute(&self, _: ffx::TargetQuery, _: &str) -> String {
            self.resp.to_string()
        }
        fn target(&self) -> Option<String> {
            None
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn route_request_test1() {
        let request = Request::builder().method("GET").uri("/foo").body(Body::empty()).unwrap();
        let proxy = MockBridgeProxy { resp: "{}" };
        let res = route_request(proxy, request).await;
        assert_eq!(res.status(), StatusCode::NOT_FOUND);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn route_request_test2() {
        let request = Request::builder().method("GET").uri("/").body(Body::empty()).unwrap();
        let proxy = MockBridgeProxy { resp: "{}" };
        let res = route_request(proxy, request).await;
        assert_eq!(res.status(), StatusCode::NOT_FOUND);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn route_request_test3() {
        let request = Request::builder().method("POST").uri("/foo").body(Body::empty()).unwrap();
        let proxy = MockBridgeProxy { resp: "{}" };
        let res = route_request(proxy, request).await;
        assert_eq!(res.status(), StatusCode::NOT_FOUND);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn route_request_test4() {
        let req_body = Body::from("{}");
        let request = Request::builder().method("POST").uri("/").body(req_body).unwrap();
        let proxy = MockBridgeProxy { resp: "{}" };
        let res = route_request(proxy, request).await;
        assert_eq!(res.status(), StatusCode::OK);
    }
}
