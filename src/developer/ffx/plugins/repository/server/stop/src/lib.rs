// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    ffx_core::ffx_plugin,
    ffx_repository_server_stop_args::StopCommand,
    fidl_fuchsia_developer_bridge::RepositoryRegistryProxy,
    fidl_fuchsia_developer_bridge_ext::RepositoryError,
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::protocol")]
pub async fn stop(_cmd: StopCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    repos.server_stop().await?.map_err(RepositoryError::from).context("failed to stop server")?;

    println!("server stopped");

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_developer_bridge::RepositoryRegistryRequest,
        fuchsia_async as fasync, futures::channel::oneshot::channel,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_stop() {
        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::ServerStop { responder } => {
                sender.take().unwrap().send(()).unwrap();
                responder.send(&mut Ok(())).unwrap()
            }
            other => panic!("Unexpected request: {:?}", other),
        });

        stop(StopCommand {}, repos).await.unwrap();
        assert_eq!(receiver.await.unwrap(), ());
    }
}
