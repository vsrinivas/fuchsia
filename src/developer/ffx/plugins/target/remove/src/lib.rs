// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, errors::ffx_error, ffx_core::ffx_plugin, ffx_target_remove_args::RemoveCommand,
    fidl_fuchsia_developer_bridge as bridge,
};

#[ffx_plugin()]
pub async fn remove(daemon_proxy: bridge::DaemonProxy, cmd: RemoveCommand) -> Result<()> {
    remove_impl(daemon_proxy, cmd, &mut std::io::stderr()).await
}

pub async fn remove_impl<W: std::io::Write>(
    daemon_proxy: bridge::DaemonProxy,
    cmd: RemoveCommand,
    err_writer: &mut W,
) -> Result<()> {
    let RemoveCommand { mut id, .. } = cmd;
    match daemon_proxy.remove_target(&mut id).await? {
        Ok(found) => {
            if found {
                writeln!(err_writer, "Removed.")?;
            } else {
                writeln!(err_writer, "No matching target found.")?;
            }
            Ok(())
        }
        Err(e) => Err(ffx_error!("Error removing target: {:?}", e).into()),
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_developer_bridge::DaemonRequest;

    fn setup_fake_daemon_server<T: 'static + Fn(String) -> bool + Send>(
        test: T,
    ) -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            DaemonRequest::RemoveTarget { target_id, responder } => {
                let result = test(target_id);
                responder.send(&mut Ok(result)).unwrap();
            }
            DaemonRequest::Quit { responder } => {
                responder.send(true).unwrap();
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove_existing_target() {
        let server = setup_fake_daemon_server(|id| {
            assert_eq!(id, "correct-horse-battery-staple".to_owned());
            true
        });
        let mut buf = Vec::new();
        remove_impl(
            server,
            RemoveCommand { id: "correct-horse-battery-staple".to_owned() },
            &mut buf,
        )
        .await
        .unwrap();

        let output = String::from_utf8(buf).unwrap();
        assert_eq!(output, "Removed.\n");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove_nonexisting_target() {
        let server = setup_fake_daemon_server(|_| false);
        let mut buf = Vec::new();
        remove_impl(
            server,
            RemoveCommand { id: "incorrect-donkey-battery-jazz".to_owned() },
            &mut buf,
        )
        .await
        .unwrap();

        let output = String::from_utf8(buf).unwrap();
        assert_eq!(output, "No matching target found.\n");
    }
}
