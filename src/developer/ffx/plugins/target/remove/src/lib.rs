// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, errors::ffx_error, ffx_core::ffx_plugin, ffx_target_remove_args::RemoveCommand,
    fidl_fuchsia_developer_bridge as bridge,
};

#[ffx_plugin()]
pub async fn remove(daemon_proxy: bridge::DaemonProxy, cmd: RemoveCommand) -> Result<()> {
    let RemoveCommand { mut id, .. } = cmd;
    if let Err(e) = daemon_proxy.remove_target(&mut id).await? {
        Err(ffx_error!("Error removing target: {:?}", e).into())
    } else {
        Ok(())
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_developer_bridge::DaemonRequest;

    fn setup_fake_daemon_server<T: 'static + Fn(String) + Send>(test: T) -> bridge::DaemonProxy {
        setup_fake_daemon_proxy(move |req| match req {
            DaemonRequest::RemoveTarget { target_id, responder } => {
                test(target_id);
                responder.send(&mut Ok(true)).unwrap();
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add() {
        let server = setup_fake_daemon_server(|id| {
            assert_eq!(id, "correct-horse-battery-staple".to_owned())
        });
        remove(server, RemoveCommand { id: "correct-horse-battery-staple".to_owned() })
            .await
            .unwrap();
    }
}
