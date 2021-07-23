// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use anyhow::Error;
use fidl::endpoints::{ClientEnd, ProtocolMarker};
use fuchsia_zircon as zx;
use glob::glob;

/// Connects to a protocol served at a path that matches a glob pattern.
///
/// Returns a proxy to the first path that matches a pattern in `glob_paths`,
/// or None if no matching paths exist.
pub fn connect_in_paths<T: ProtocolMarker>(glob_paths: &[&str]) -> Result<Option<T::Proxy>, Error> {
    let proxy = glob_paths
        .iter()
        .map(|glob_path| {
            let found_path = glob(glob_path)?.find_map(Result::ok);
            match found_path {
                Some(path) => {
                    let (client, server) = zx::Channel::create()?;
                    fdio::service_connect(path.to_string_lossy().as_ref(), server)?;
                    let client_end = ClientEnd::<T>::new(client);
                    Ok(Some(client_end.into_proxy()?))
                }
                None => Ok(None),
            }
        })
        .collect::<Result<Vec<Option<T::Proxy>>, Error>>()?
        .into_iter()
        .find(|proxy| proxy.is_some())
        .flatten();
    Ok(proxy)
}
