// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Triggers a forced fdr by comparing the configured
//! index against the stored index

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_recovery::{FactoryResetMarker, FactoryResetProxy},
    fidl_fuchsia_update_channel::{ProviderMarker, ProviderProxy},
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{fx_log_info, fx_log_warn},
    serde::{Deserialize, Serialize},
    std::collections::HashMap,
    std::fs,
    std::fs::File,
    std::path::PathBuf,
};

const DEVICE_INDEX_FILE: &str = "stored-index.json";
const CONFIGURED_INDEX_FILE: &str = "forced-fdr-channel-indices.config";

#[derive(Serialize, Deserialize, Debug)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum ChannelIndices {
    #[serde(rename = "1")]
    Version1 { channel_indices: HashMap<String, i32> },
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum StoredIndex {
    #[serde(rename = "1")]
    Version1 { channel: String, index: i32 },
}

struct ForcedFDR {
    data_dir: PathBuf,
    config_data_dir: PathBuf,
    info_proxy: ProviderProxy,
    factory_reset_proxy: FactoryResetProxy,
}

impl ForcedFDR {
    fn new() -> Result<Self, Error> {
        let info_proxy = connect_to_service::<ProviderMarker>()?;
        let factory_reset_proxy = connect_to_service::<FactoryResetMarker>()?;

        Ok(ForcedFDR {
            data_dir: "/data".into(),
            config_data_dir: "/config/data".into(),
            info_proxy: info_proxy,
            factory_reset_proxy: factory_reset_proxy,
        })
    }

    #[cfg(test)]
    fn new_mock(
        data_dir: PathBuf,
        config_data_dir: PathBuf,
    ) -> (
        Self,
        fidl_fuchsia_update_channel::ProviderRequestStream,
        fidl_fuchsia_recovery::FactoryResetRequestStream,
    ) {
        let (info_proxy, info_stream) =
            fidl::endpoints::create_proxy_and_stream::<ProviderMarker>().unwrap();
        let (fdr_proxy, fdr_stream) =
            fidl::endpoints::create_proxy_and_stream::<FactoryResetMarker>().unwrap();

        (
            ForcedFDR {
                data_dir: data_dir,
                config_data_dir: config_data_dir,
                info_proxy: info_proxy,
                factory_reset_proxy: fdr_proxy,
            },
            info_stream,
            fdr_stream,
        )
    }
}

/// Performs a Factory Data Reset(FDR) on the device "if necessary." Necessity
/// is determined by comparing the index stored in `forced-fdr-channel-indices.config`
/// for the device's ota channel against an index written into storage that
/// represents the last successful FDR. `forced-fdr-channel-indices.config` is
/// provided on a per board basis using config-data.
///
/// # Errors
///
/// There are numerous cases (config missing, failed to read file, ...)
/// where the library is unable to determine if an FDR is necessary. In
/// all of these cases, the library will *not* FDR.
pub async fn perform_fdr_if_necessary() {
    perform_fdr_if_necessary_impl()
        .await
        .unwrap_or_else(|err| fx_log_info!(tag: "forced-fdr", "{:?}\n", err))
}

async fn perform_fdr_if_necessary_impl() -> Result<(), Error> {
    let forced_fdr = ForcedFDR::new().context("Failed to connect to required services")?;
    run(forced_fdr).await
}

async fn run(fdr: ForcedFDR) -> Result<(), Error> {
    let current_channel =
        get_current_channel(&fdr).await.context("Failed to get current channel")?;

    let channel_indices = get_channel_indices(&fdr).context("Channel indices not available")?;

    if !is_channel_in_allowlist(&channel_indices, &current_channel) {
        return Err(format_err!("Not in forced FDR allowlist"));
    }

    let channel_index = get_channel_index(&channel_indices, &current_channel)
        .ok_or(format_err!("Not in forced FDR allowlist."))?;

    let device_index = match get_stored_index(&fdr, &current_channel) {
        Ok(index) => index,
        Err(err) => {
            fx_log_info!("Unable to read stored index: {}", err);
            // The device index is missing so it should be
            // written in preparation for the next FDR ota.
            // The index will always be missing right after
            // an FDR.
            return Ok(write_stored_index(&fdr, &current_channel, channel_index)
                .context("Failed to write device index")?);
        }
    };

    if device_index >= channel_index {
        return Err(format_err!("FDR not required"));
    }

    trigger_fdr(&fdr).await.context("Failed to trigger FDR")?;

    Ok(())
}

fn get_channel_indices(fdr: &ForcedFDR) -> Result<HashMap<String, i32>, Error> {
    let f = open_channel_indices_file(fdr)?;
    match serde_json::from_reader(std::io::BufReader::new(f))? {
        ChannelIndices::Version1 { channel_indices } => Ok(channel_indices),
    }
}

fn open_channel_indices_file(fdr: &ForcedFDR) -> Result<File, Error> {
    Ok(fs::File::open(fdr.config_data_dir.join(CONFIGURED_INDEX_FILE))?)
}

async fn get_current_channel(fdr: &ForcedFDR) -> Result<String, Error> {
    Ok(fdr.info_proxy.get_current().await?)
}

fn is_channel_in_allowlist(allowlist: &HashMap<String, i32>, channel: &String) -> bool {
    allowlist.contains_key(channel)
}

fn get_channel_index(channel_indices: &HashMap<String, i32>, channel: &String) -> Option<i32> {
    channel_indices.get(channel).map(|i| *i)
}

async fn trigger_fdr(fdr: &ForcedFDR) -> Result<i32, Error> {
    fx_log_warn!("Triggering FDR. SSH keys will be lost\n");
    Ok(fdr.factory_reset_proxy.reset().await?)
}

fn get_stored_index(fdr: &ForcedFDR, current_channel: &String) -> Result<i32, Error> {
    let f = open_stored_index_file(fdr)?;
    match serde_json::from_reader(std::io::BufReader::new(f))? {
        StoredIndex::Version1 { channel, index } => {
            // The channel has been changed, and thus nothing can be assumed.
            // Report error so the file will be replaced with the file for
            // the new channel.
            if *current_channel != channel {
                return Err(format_err!("Mismatch between stored and current channel"));
            }

            Ok(index)
        }
    }
}

fn open_stored_index_file(fdr: &ForcedFDR) -> Result<File, Error> {
    Ok(fs::File::open(fdr.data_dir.join(DEVICE_INDEX_FILE))?)
}

fn write_stored_index(fdr: &ForcedFDR, channel: &String, index: i32) -> Result<(), Error> {
    fx_log_info!("Writing index {} for channel {}\n", index, channel);
    let stored_index = StoredIndex::Version1 { channel: channel.to_string(), index: index };
    let contents = serde_json::to_string(&stored_index)?;
    fs::write(fdr.data_dir.join(DEVICE_INDEX_FILE), contents)?;
    Ok(())
}

#[cfg(test)]
mod forced_fdr_test;
