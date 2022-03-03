// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_repository_server_start_args::StartCommand,
    fidl_fuchsia_developer_ffx::RepositoryRegistryProxy,
    fidl_fuchsia_developer_ffx_ext::RepositoryError,
    fidl_fuchsia_net_ext::SocketAddress,
};

#[ffx_plugin("ffx_repository", RepositoryRegistryProxy = "daemon::protocol")]
pub async fn start(_cmd: StartCommand, repos: RepositoryRegistryProxy) -> Result<()> {
    match repos
        .server_start()
        .await
        .context("communicating with daemon")?
        .map_err(RepositoryError::from)
    {
        Ok(address) => {
            let address = SocketAddress::from(address);
            println!("server is listening on {}", address);

            Ok(())
        }
        Err(RepositoryError::ServerNotRunning) => {
            ffx_bail!(
                "Failed to start repository server: {:#}",
                pkg::config::determine_why_repository_server_is_not_running().await
            )
        }
        Err(err) => {
            ffx_bail!("Failed to start repository server: {}", err)
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_developer_ffx::{RepositoryError, RepositoryRegistryRequest},
        fuchsia_async as fasync,
        futures::channel::oneshot::channel,
        std::net::Ipv4Addr,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_start() {
        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::ServerStart { responder } => {
                sender.take().unwrap().send(()).unwrap();
                let address = SocketAddress((Ipv4Addr::LOCALHOST, 0).into()).into();
                responder.send(&mut Ok(address)).unwrap()
            }
            other => panic!("Unexpected request: {:?}", other),
        });

        start(StartCommand {}, repos).await.unwrap();
        assert_eq!(receiver.await.unwrap(), ());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_start_failed() {
        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoryRegistryRequest::ServerStart { responder } => {
                sender.take().unwrap().send(()).unwrap();
                responder.send(&mut Err(RepositoryError::ServerNotRunning)).unwrap()
            }
            other => panic!("Unexpected request: {:?}", other),
        });

        assert!(start(StartCommand {}, repos).await.is_err());
        assert_eq!(receiver.await.unwrap(), ());
    }
}
