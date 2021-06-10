// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
mod fake_daemon;

pub use fake_daemon::{FakeDaemon, FakeDaemonBuilder};

use {
    crate::{Context, FidlService},
    fidl::endpoints::create_endpoints,
    fidl::endpoints::Proxy,
    fidl::{endpoints::ServiceMarker, AsyncChannel},
    fuchsia_async::Task,
    std::cell::RefCell,
    std::rc::Rc,
};

/// A simple proxy made from a FIDL service. This is necessary if your proxy
/// has some specific state you would like to have control over. You can inspect
/// the service's internals or call specific functions via use of this method.
///
/// The lifetime of the FIDL service is as follows:
/// * invokes start, panicking on failure.
/// * create a `fuchsia_async::Task<()>` which, inside:
///   * invokes serve, panicking on failure.
///   * invokes stop at the end of `serve`, panicking on failure.
///
/// Note: the proxy you receive isn't registered with the FakeDaemon. If you
/// would like to test the `stop` functionality, you will need to drop
/// the proxy returned by this function, then await the returned task.
pub async fn create_proxy<F: FidlService + 'static>(
    f: Rc<RefCell<F>>,
    fake_daemon: &FakeDaemon,
) -> (<F::Service as ServiceMarker>::Proxy, Task<()>) {
    let (client, server) = create_endpoints::<F::Service>().unwrap();
    let client = AsyncChannel::from_channel(client.into_channel()).unwrap();
    let client = <F::Service as ServiceMarker>::Proxy::from_channel(client);
    let cx = Context::new(fake_daemon.clone());
    let svc = f.clone();
    svc.borrow_mut().start(&cx).await.unwrap();
    let task = Task::local(async move {
        let stream = server.into_stream().unwrap();
        svc.borrow().serve(&cx, stream).await.unwrap();
        svc.borrow_mut().stop(&cx).await.unwrap();
    });
    (client, task)
}
