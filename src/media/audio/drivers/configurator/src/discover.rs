// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::codec::CodecInterface;
use crate::configurator::Configurator;
use anyhow::{format_err, Error};
use fidl_fuchsia_io;
use futures::TryStreamExt;
use std::path::Path;

/// Finds any codec devices and calls the `process_new_codec` Configurator callback.
/// If `break_on_idle` is true then not finding a codec is an error.
/// If `dev_proxy` can't be cloned an error is returned.
pub async fn find_codecs<T: Configurator>(
    dev_proxy: fidl_fuchsia_io::DirectoryProxy,
    break_on_idle: bool,
    mut configurator: T,
) -> Result<(), Error> {
    let dev_proxy_local = io_util::clone_directory(&dev_proxy, 0)?;
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(dev_proxy_local).await?;

    let mut found_codec = false;

    while let Some(msg) = watcher.try_next().await? {
        match msg.event {
            fuchsia_vfs_watcher::WatchEvent::EXISTING
            | fuchsia_vfs_watcher::WatchEvent::ADD_FILE => {
                let dev_proxy_local = io_util::clone_directory(&dev_proxy, 0);
                match dev_proxy_local {
                    Ok(local) => {
                        let path = Path::new(&msg.filename);
                        tracing::info!("Found new codec in devfs node: {:?}", path);
                        let interface = CodecInterface::new(local, &path);
                        configurator.process_new_codec(interface).await;
                        found_codec = true;
                    }
                    Err(e) => tracing::warn!("Error in devfs proxy: {}", e),
                }
            }
            fuchsia_vfs_watcher::WatchEvent::IDLE => {
                if break_on_idle {
                    if !found_codec {
                        return Err(format_err!("No codec found and breaking on idle"));
                    } else {
                        return Ok(());
                    }
                }
            }
            _ => (),
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::tests::get_dev_proxy;
    use crate::testing::tests::NullConfigurator;
    use anyhow::Result;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_find_codecs() -> Result<()> {
        let (_realm_instance, dev_proxy) = get_dev_proxy().await?;
        let configurator = NullConfigurator::new();
        find_codecs(dev_proxy, true, configurator).await?;
        Ok(())
    }
}
