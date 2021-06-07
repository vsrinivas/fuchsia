// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, errors::ffx_bail, ffx_core::ffx_plugin,
    ffx_repository_remove_args::RemoveCommand, fidl_fuchsia_developer_bridge::RepositoriesProxy,
};

#[ffx_plugin(RepositoriesProxy = "daemon::service")]
pub async fn remove(cmd: RemoveCommand, repos: RepositoriesProxy) -> Result<()> {
    if !repos.remove(&cmd.name).await? {
        ffx_bail!("No repository named \"{}\".", cmd.name);
    }

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        fidl_fuchsia_developer_bridge::RepositoriesRequest, fuchsia_async as fasync,
        futures::channel::oneshot::channel,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_remove() {
        let (sender, receiver) = channel();
        let mut sender = Some(sender);
        let repos = setup_fake_repos(move |req| match req {
            RepositoriesRequest::Remove { name, responder } => {
                sender.take().unwrap().send(name).unwrap();
                responder.send(true).unwrap()
            }
            other => panic!("Unexpected request: {:?}", other),
        });
        remove(RemoveCommand { name: "MyRepo".to_owned() }, repos).await.unwrap();
        assert_eq!(receiver.await.unwrap(), "MyRepo".to_owned());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_remove_fail() {
        let repos = setup_fake_repos(move |req| match req {
            RepositoriesRequest::Remove { responder, .. } => responder.send(false).unwrap(),
            other => panic!("Unexpected request: {:?}", other),
        });
        assert!(remove(RemoveCommand { name: "NotMyRepo".to_owned() }, repos).await.is_err());
    }
}
