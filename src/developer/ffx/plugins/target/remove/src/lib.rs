// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_target_remove_args::RemoveCommand,
    fidl_fuchsia_developer_bridge::TargetCollectionProxy,
};

#[ffx_plugin(TargetCollectionProxy = "daemon::service")]
pub async fn remove(target_collection: TargetCollectionProxy, cmd: RemoveCommand) -> Result<()> {
    remove_impl(target_collection, cmd, &mut std::io::stderr()).await
}

pub async fn remove_impl<W: std::io::Write>(
    target_collection: TargetCollectionProxy,
    cmd: RemoveCommand,
    err_writer: &mut W,
) -> Result<()> {
    let RemoveCommand { mut id, .. } = cmd;
    if target_collection.remove_target(&mut id).await? {
        writeln!(err_writer, "Removed.")?;
    } else {
        writeln!(err_writer, "No matching target found.")?;
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_developer_bridge as bridge;

    fn setup_fake_target_collection_proxy<T: 'static + Fn(String) -> bool + Send>(
        test: T,
    ) -> TargetCollectionProxy {
        setup_fake_target_collection(move |req| match req {
            bridge::TargetCollectionRequest::RemoveTarget { target_id, responder } => {
                let result = test(target_id);
                responder.send(result).unwrap();
            }
            _ => assert!(false),
        })
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_remove_existing_target() {
        let server = setup_fake_target_collection_proxy(|id| {
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
        let server = setup_fake_target_collection_proxy(|_| false);
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
