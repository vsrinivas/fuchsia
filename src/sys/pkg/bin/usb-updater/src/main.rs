// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(unused_imports)]
use anyhow::{self, Context};
use fidl_fuchsia_pkg::{PackageUrl, RepositoryManagerMarker};
use fidl_fuchsia_pkg::{RepositoryConfig, RepositoryKeyConfig};
use fidl_fuchsia_pkg_ext::{RepositoryConfigBuilder, RepositoryKey};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

pub fn get_key(hex: &str) -> RepositoryKey {
    RepositoryKey::Ed25519(
        (0..hex.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&hex[i..i + 2], 16))
            .collect::<Result<Vec<u8>, std::num::ParseIntError>>()
            .unwrap(),
    )
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    let config = RepositoryConfigBuilder::new("fuchsia-pkg://fuchsia.com".parse().unwrap())
        .add_root_key(get_key("be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307"))
        .use_local_mirror(true)
        .build();

    let manager = fuchsia_component::client::connect_to_service::<RepositoryManagerMarker>()
        .expect("connect manager ok");
    manager.add(config.into()).await.expect("send ok").expect("add ok");

    let svc =
        fuchsia_component::client::connect_to_service::<fidl_fuchsia_update_usb::CheckerMarker>()
            .expect("connect OK");

    svc.check(&mut PackageUrl { url: "fuchsia-pkg://fuchsia.com/update".to_owned() }, None, None)
        .await
        .expect("send check succeeds")
        .expect("update succeeds");

    eprintln!("update OK!");

    Ok(())
}
