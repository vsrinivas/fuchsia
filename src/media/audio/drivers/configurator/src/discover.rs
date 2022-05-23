// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::codec::CodecInterface;
use crate::configurator::Configurator;
use anyhow::Error;
use fidl_fuchsia_io as fio;
use futures::TryStreamExt;
use std::path::Path;

/// Finds any codec devices and calls the `process_new_codec` Configurator callback.
/// If `break_count` is non-zero then once `break_count` codecs are found return.
/// If `dev_proxy` can't be cloned an error is returned.
pub async fn find_codecs<T: Configurator>(
    dev_proxy: fio::DirectoryProxy,
    break_count: u32,
    mut configurator: T,
) -> Result<(), Error> {
    let dev_proxy_local = io_util::clone_directory(&dev_proxy, fio::OpenFlags::empty())?;
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(dev_proxy_local).await?;

    let mut codecs_found = 0;

    while let Some(msg) = watcher.try_next().await? {
        match msg.event {
            fuchsia_vfs_watcher::WatchEvent::EXISTING
            | fuchsia_vfs_watcher::WatchEvent::ADD_FILE => {
                let dev_proxy_local = io_util::clone_directory(&dev_proxy, fio::OpenFlags::empty());
                match dev_proxy_local {
                    Ok(local) => {
                        let path = Path::new(&msg.filename);
                        tracing::info!("Found new codec in devfs node: {:?}", path);
                        let interface = CodecInterface::new(local, &path);
                        configurator.process_new_codec(interface).await;
                        codecs_found += 1;
                        if codecs_found == break_count {
                            return Ok(());
                        }
                    }
                    Err(e) => tracing::warn!("Error in devfs proxy: {}", e),
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
        find_codecs(dev_proxy, 2, configurator).await?;
        Ok(())
    }
}
