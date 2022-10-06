// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    fidl_fuchsia_device as fdevice, fuchsia_zircon as zx,
    serde_derive::Deserialize,
    std::collections::HashMap,
    tracing::{error, info},
};

#[derive(Deserialize)]
pub struct DriverAlias {
    /// Human-readable alias.
    pub name: String,
    /// Topological path.
    pub topological_path: String,
}

/// Helper struct to deserialize the optional config file loaded from CONFIG_PATH
/// (//src/testing/metrics-logger/src/main.rs). The config file maps the human-readable aliases
/// to the topological paths of the drivers.
#[derive(Deserialize)]
pub struct Config {
    pub temperature_drivers: Option<Vec<DriverAlias>>,
    pub power_drivers: Option<Vec<DriverAlias>>,
    pub gpu_drivers: Option<Vec<DriverAlias>>,
}

pub fn connect_proxy<T: fidl::endpoints::ProtocolMarker>(path: &str) -> Result<T::Proxy> {
    let (proxy, server) = fidl::endpoints::create_proxy::<T>()
        .map_err(|e| format_err!("Failed to create proxy: {}", e))?;

    fdio::service_connect(path, server.into_channel())
        .map_err(|s| format_err!("Failed to connect to service at {}: {}", path, s))?;
    Ok(proxy)
}

/// Maps from devices' topological paths to their class paths in the provided directories.
pub async fn map_topo_paths_to_class_paths(
    service_dirs: &[&str],
) -> Result<HashMap<String, String>> {
    let mut path_map = HashMap::new();
    for dir_path in service_dirs {
        let drivers = list_drivers(dir_path).await;
        for driver in drivers.iter() {
            let class_path = format!("{}/{}", dir_path, driver);
            let topo_path = get_driver_topological_path(&class_path).await?;
            path_map.insert(topo_path, class_path);
        }
    }
    Ok(path_map)
}

async fn get_driver_topological_path(path: &str) -> Result<String> {
    let proxy = connect_proxy::<fdevice::ControllerMarker>(path)?;
    proxy
        .get_topological_path()
        .await?
        .map_err(|raw| format_err!("zx error: {}", zx::Status::from_raw(raw)))
}

pub async fn list_drivers(path: &str) -> Vec<String> {
    let dir = match fuchsia_fs::directory::open_in_namespace(
        path,
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    ) {
        Ok(s) => s,
        Err(err) => {
            info!(%path, %err, "Service directory doesn't exist or NodeProxy failed with error");
            return Vec::new();
        }
    };
    match fuchsia_fs::directory::readdir(&dir).await {
        Ok(s) => s.iter().map(|dir_entry| dir_entry.name.clone()).collect(),
        Err(err) => {
            error!(%path, %err, "Read service directory failed with error");
            Vec::new()
        }
    }
}

// Representation of an actively-used driver.
pub struct Driver<T> {
    pub alias: Option<String>,
    pub topological_path: String,
    pub proxy: T,
}

impl<T> Driver<T> {
    pub fn name(&self) -> &str {
        &self.alias.as_ref().unwrap_or(&self.topological_path)
    }
}
