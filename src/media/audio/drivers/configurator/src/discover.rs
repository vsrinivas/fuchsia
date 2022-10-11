// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{codec::CodecInterface, configurator::Configurator, dai::DaiInterface},
    anyhow::{anyhow, Error},
    fidl_fuchsia_io as fio,
    futures::{lock::Mutex, TryStreamExt},
    std::{path::Path, sync::Arc},
};

/// Finds any codec devices and calls the `process_new_codec` Configurator callback.
/// If `break_count` is non-zero then once `break_count` codecs are found return.
/// If `dev_proxy` can't be cloned an error is returned.
pub async fn find_codecs<T: Configurator>(
    dev_proxy: fio::DirectoryProxy,
    break_count: u32,
    configurator: Arc<Mutex<T>>,
) -> Result<(), Error> {
    let dev_proxy_local = fuchsia_fs::clone_directory(&dev_proxy, fio::OpenFlags::empty())?;
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(dev_proxy_local).await?;

    let mut codecs_found = 0;

    while let Some(msg) = watcher.try_next().await? {
        match msg.event {
            fuchsia_vfs_watcher::WatchEvent::EXISTING
            | fuchsia_vfs_watcher::WatchEvent::ADD_FILE => {
                if msg.filename == Path::new(".") {
                    continue;
                }
                let dev_proxy_local =
                    fuchsia_fs::clone_directory(&dev_proxy, fio::OpenFlags::empty());
                match dev_proxy_local {
                    Ok(local) => {
                        let path = Path::new(&msg.filename);
                        tracing::info!("Found new codec in devfs node: {:?}", path);
                        let interface = CodecInterface::new(local, &path);
                        if let Err(e) = configurator.lock().await.process_new_codec(interface).await
                        {
                            if break_count != 0 {
                                // Error when we want to break on count, then report it and exit.
                                return Err(anyhow!("Codec processing error: {:?}", e));
                            } else {
                                // Otherwise we continue finding codecs.
                                tracing::warn!("Codec processing error: {:?}", e);
                            }
                        }
                        codecs_found += 1;
                        if codecs_found == break_count {
                            return Ok(());
                        }
                    }
                    Err(e) => tracing::warn!("Error in devfs proxy: {}", e),
                }
            }
            _ => continue,
        }
    }
    Ok(())
}

/// Finds any DAI devices and calls the `process_new_dai` Configurator callback.
/// If `break_count` is non-zero then once `break_count` DAIs are found return.
/// If `dev_proxy` can't be cloned an error is returned.
pub async fn find_dais<T: Configurator>(
    dev_proxy: fio::DirectoryProxy,
    break_count: u32,
    configurator: Arc<Mutex<T>>,
) -> Result<(), Error> {
    let dev_proxy_local = fuchsia_fs::clone_directory(&dev_proxy, fio::OpenFlags::empty())?;
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(dev_proxy_local).await?;

    let mut dais_found = 0;
    let mut stream_config_serve_tasks = Vec::new();

    while let Some(msg) = watcher.try_next().await? {
        match msg.event {
            fuchsia_vfs_watcher::WatchEvent::EXISTING
            | fuchsia_vfs_watcher::WatchEvent::ADD_FILE => {
                if msg.filename == Path::new(".") {
                    continue;
                }
                let dev_proxy_local =
                    fuchsia_fs::clone_directory(&dev_proxy, fio::OpenFlags::empty());
                match dev_proxy_local {
                    Ok(local) => {
                        let path = Path::new(&msg.filename);
                        tracing::info!("Found new DAI in devfs node: {:?}", path);
                        let interface = DaiInterface::new(local, &path);
                        let mut configurator = configurator.lock().await;
                        if let Err(e) = configurator.process_new_dai(interface).await {
                            if break_count != 0 {
                                // Error when we want to break on count, then report it and exit.
                                return Err(anyhow!("DAI processing error: {:?}", e));
                            } else {
                                // Otherwise we continue finding DAIs.
                                tracing::warn!("DAI processing error: {:?}", e);
                            }
                        } else {
                            // If we are not breaking then serve the required interfaces.
                            if break_count == 0 {
                                match configurator.serve_interface() {
                                    Err(e) => tracing::warn!("Interface serving error: {:?}", e),
                                    Ok(task) => {
                                        stream_config_serve_tasks.push(task);
                                    }
                                }
                            }
                        }
                        dais_found += 1;
                        if dais_found == break_count {
                            return Ok(());
                        }
                    }
                    Err(e) => tracing::warn!("Error in devfs proxy: {}", e),
                }
            }
            _ => continue,
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            config::Config,
            testing::tests::{get_dev_proxy, NullConfigurator},
        },
        anyhow::Result,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_find_codecs() -> Result<()> {
        let (_realm_instance, dev_proxy) = get_dev_proxy("class/codec").await?;
        let config = Config::new()?;
        let configurator = Arc::new(Mutex::new(NullConfigurator::new(config)?));
        find_codecs(dev_proxy, 2, configurator).await?;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_find_dais() -> Result<()> {
        let (_realm_instance, dev_proxy) = get_dev_proxy("class/dai").await?;
        let config = Config::new()?;
        let configurator = Arc::new(Mutex::new(NullConfigurator::new(config)?));
        find_dais(dev_proxy, 1, configurator).await?;
        Ok(())
    }
}
