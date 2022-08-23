// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    ffx_core::ffx_plugin,
    ffx_session_add_args::SessionAddCommand,
    fidl_fuchsia_element::{ControllerMarker, ManagerProxy, Spec},
    futures::{channel::oneshot, FutureExt},
    signal_hook::{consts::signal::*, iterator::Signals},
    std::future::Future,
};

#[ffx_plugin(ManagerProxy = "core/session-manager:expose:fuchsia.element.Manager")]
pub async fn add(manager_proxy: ManagerProxy, cmd: SessionAddCommand) -> Result<()> {
    add_impl(manager_proxy, cmd, spawn_ctrl_c_listener(), &mut std::io::stdout()).await
}

pub async fn add_impl<W: std::io::Write>(
    manager_proxy: ManagerProxy,
    cmd: SessionAddCommand,
    ctrl_c_signal: impl Future<Output = ()>,
    writer: &mut W,
) -> Result<()> {
    writeln!(writer, "Add {} to the current session", &cmd.url)?;
    let arguments = if cmd.args.is_empty() { None } else { Some(cmd.args) };

    let (controller_client, controller_server) = if cmd.interactive {
        let (client, server) = fidl::endpoints::create_endpoints::<ControllerMarker>()
            .context("creating element controller")?;
        let client = client.into_proxy().context("converting client end to proxy")?;
        (Some(client), Some(server))
    } else {
        (None, None)
    };

    manager_proxy
        .propose_element(
            Spec { component_url: Some(cmd.url.to_string()), arguments, ..Spec::EMPTY },
            controller_server,
        )
        .await?
        .map_err(|err| format_err!("{:?}", err))?;

    if controller_client.is_some() {
        // TODO(https://fxbug.dev/107543) wait for either ctrl+c or the controller to close
        writeln!(writer, "Waiting for Ctrl+C before terminating element...")?;
        ctrl_c_signal.await;
    }

    Ok(())
}

/// Spawn a thread to listen for Ctrl+C, resolving the returned future when it's received.
fn spawn_ctrl_c_listener() -> impl Future<Output = ()> {
    let (sender, receiver) = oneshot::channel();
    std::thread::spawn(move || {
        let mut signals = Signals::new(&[SIGINT]).expect("must be able to create signal waiter");
        signals.forever().next().unwrap();
        sender.send(()).unwrap();
    });
    receiver.map(|_| ())
}

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_element::ManagerRequest, futures::poll, lazy_static::lazy_static};

    #[fuchsia::test]
    async fn test_add_element() {
        const TEST_ELEMENT_URL: &str = "Test Element Url";

        let proxy = setup_fake_manager_proxy(|req| match req {
            ManagerRequest::ProposeElement { spec, responder, .. } => {
                assert_eq!(spec.component_url.unwrap(), TEST_ELEMENT_URL.to_string());
                let _ = responder.send(&mut Ok(()));
            }
        });

        let add_cmd = SessionAddCommand {
            url: TEST_ELEMENT_URL.to_string(),
            args: vec![],
            interactive: false,
        };
        let response = add(proxy, add_cmd).await;
        assert!(response.is_ok());
    }

    #[fuchsia::test]
    async fn test_add_element_args() {
        const TEST_ELEMENT_URL: &str = "Test Element Url";
        lazy_static! {
            static ref TEST_ARGS: Vec<String> = vec!["hello".to_string(), "world".to_string()];
        }

        let proxy = setup_fake_manager_proxy(|req| match req {
            ManagerRequest::ProposeElement { spec, responder, .. } => {
                let arguments = spec.arguments.expect("spec does not have annotations field set");
                assert_eq!(arguments, *TEST_ARGS);
                let _ = responder.send(&mut Ok(()));
            }
        });

        let add_cmd = SessionAddCommand {
            url: TEST_ELEMENT_URL.to_string(),
            args: TEST_ARGS.clone(),
            interactive: false,
        };
        let response = add(proxy, add_cmd).await;
        assert!(response.is_ok());
    }

    #[fuchsia::test]
    async fn test_add_interactive_element_stop_with_ctrl_c() {
        const TEST_ELEMENT_URL: &str = "Test Element Url";
        lazy_static! {
            static ref TEST_ARGS: Vec<String> = vec!["hello".to_string(), "world".to_string()];
        }

        let proxy = setup_fake_manager_proxy(move |req| match req {
            ManagerRequest::ProposeElement { responder, .. } => {
                responder.send(&mut Ok(())).unwrap();
            }
        });

        let add_cmd = SessionAddCommand {
            url: TEST_ELEMENT_URL.to_string(),
            args: TEST_ARGS.clone(),
            interactive: true,
        };
        let (ctrl_c_sender, ctrl_c_receiver) = oneshot::channel();
        let mut stdout = std::io::stdout();
        let mut add_fut =
            Box::pin(add_impl(proxy, add_cmd, ctrl_c_receiver.map(|_| ()), &mut stdout));

        assert!(poll!(&mut add_fut).is_pending(), "add should yield until ctrl+c");

        // Send ctrl+c so add will exit.
        ctrl_c_sender.send(()).unwrap();
        let result = add_fut.await;
        assert!(result.is_ok());
    }
}
