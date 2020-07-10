// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_logger::{LogMarker, LogMessage, LogProxy},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client as fclient,
    fuchsia_syslog_listener::{run_log_listener_with_proxy, LogProcessor},
    futures::{channel::mpsc, Stream, StreamExt},
};

struct Listener {
    send_logs: mpsc::UnboundedSender<LogMessage>,
}

impl LogProcessor for Listener {
    fn log(&mut self, message: LogMessage) {
        self.send_logs.unbounded_send(message).unwrap();
    }

    fn done(&mut self) {
        panic!("this should not be called");
    }
}

fn run_listener(log_proxy: LogProxy) -> impl Stream<Item = LogMessage> {
    let (send_logs, recv_logs) = mpsc::unbounded();
    let l = Listener { send_logs };
    fasync::spawn(async move {
        let fut = run_log_listener_with_proxy(&log_proxy, l, None, false, None);
        if let Err(e) = fut.await {
            panic!("test fail {:?}", e);
        }
    });
    recv_logs
}

#[fasync::run_singlethreaded(test)]
async fn hello_world_integration_test() -> Result<(), Error> {
    // Connect to the realm service, so that we can bind to and interact with child components
    let realm_proxy = fclient::realm()?;

    // The hello world component will connect to the /svc/fuchsia.logger.LogSink protocol provided
    // by the observer component. Let's bind to the observer component by connecting to
    // /svc/fuchsia.logger.Log, which will cause the component to start, and begin watching the log
    // stream before starting the hello_world component, so we can see its message
    let log_proxy = fclient::connect_to_service::<LogMarker>()?;
    let log_stream = run_listener(log_proxy);

    // Now that we're connected to the log server, let's use the fuchsia.sys2.Realm protocol to
    // manually bind to our hello_world child, which will cause it to start. Once started, the
    // hello_world component will connect to the observer component to send its hello world log
    // message
    let (_hello_world_proxy, hello_world_server_end) = create_proxy::<DirectoryMarker>()?;
    realm_proxy
        .bind_child(
            &mut fsys::ChildRef { name: "hello_world".to_string(), collection: None },
            hello_world_server_end,
        )
        .await?
        .map_err(|e| anyhow!("failed to bind to hello_world: {:?}", e))?;

    // We should see two log messages, one that states that logging started and the hello world
    // message we're expecting.
    let mut logs: Vec<_> = log_stream.take(2).collect().await;

    // sort logs to account for out-of-order arrival
    logs.sort_by(|a, b| a.msg.cmp(&b.msg));

    assert_eq!(2, logs.len(), "log stream closed unexpectedly");
    assert_eq!(logs[0].msg, "Hippo: Hello World!");
    assert_eq!(logs[1].msg, "Logging started.");

    Ok(())
}
