// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Triggers a forced fdr by comparing the configured
//! index against the stored index

use {
    failure::{bail, format_err, Error, ResultExt},
    fidl_fuchsia_recovery::{FactoryResetMarker, FactoryResetProxy},
    fidl_fuchsia_update_channel::{ProviderMarker, ProviderProxy},
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{fx_log_info, fx_log_warn},
    serde_derive::{Deserialize, Serialize},
    std::collections::HashMap,
    std::fs,
    std::fs::File,
    std::path::PathBuf,
    std::str::FromStr,
};

const DEVICE_INDEX_FILE: &str = "previous-forced-fdr-index";
const CONFIGURED_INDEX_FILE: &str = "forced-fdr-channel-indices.config";

#[derive(Serialize, Deserialize, Debug)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum ChannelIndices {
    #[serde(rename = "1")]
    Version1 { channel_indices: HashMap<String, i32> },
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

    if !is_channel_in_whitelist(&channel_indices, &current_channel) {
        bail!("Not in whitelist");
    }

    let channel_index = get_channel_index(&channel_indices, &current_channel)
        .ok_or(format_err!("Not in whitelist."))?;

    let device_index = match get_device_index(&fdr) {
        Ok(index) => index,
        Err(_) => {
            // The device index is missing so it should be
            // written in preparation for the next FDR ota.
            // The index will always be missing right after
            // an FDR.
            return Ok(
                write_device_index(&fdr, channel_index).context("Failed to write device index")?
            );
        }
    };

    if device_index >= channel_index {
        bail!("FDR not required");
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

fn get_device_index(fdr: &ForcedFDR) -> Result<i32, Error> {
    let content = fs::read_to_string(fdr.data_dir.join(DEVICE_INDEX_FILE))?;
    Ok(i32::from_str(&content)?)
}

fn is_channel_in_whitelist(whitelist: &HashMap<String, i32>, channel: &String) -> bool {
    whitelist.contains_key(channel)
}

fn get_channel_index(channel_indices: &HashMap<String, i32>, channel: &String) -> Option<i32> {
    channel_indices.get(channel).map(|i| *i)
}

async fn trigger_fdr(fdr: &ForcedFDR) -> Result<i32, Error> {
    fx_log_warn!(tag: "forced-fdr", "Triggering FDR. SSH keys will be lost\n");
    Ok(fdr.factory_reset_proxy.reset().await?)
}

fn write_device_index(fdr: &ForcedFDR, index: i32) -> Result<(), Error> {
    fx_log_info!(tag: "forced-fdr", "Writing index {}\n", index);
    fs::write(fdr.data_dir.join(DEVICE_INDEX_FILE), index.to_string())?;
    Ok(())
}

#[cfg(test)]
mod forced_fdr_test;
