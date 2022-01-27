// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::{DirectoryProxy, OPEN_RIGHT_READABLE},
    fidl_fuchsia_pkg_ext::RepositoryConfigs,
    files_async::readdir,
    io_util::{directory::open_file, file::read},
    security_pkg_test_util_host::hostname_from_vec,
    serde_json::from_slice,
};

pub async fn hostname_from_pkg_resolver_directory(
    pkg_resolver_repositories_dir: &DirectoryProxy,
) -> String {
    let pkg_resolver_repositories_entries = readdir(&pkg_resolver_repositories_dir).await.unwrap();

    assert!(pkg_resolver_repositories_entries.len() > 0);

    let mut hostnames = vec![];

    for pkg_resolver_repository in pkg_resolver_repositories_entries.into_iter() {
        let repository_config_file = open_file(
            &pkg_resolver_repositories_dir,
            &pkg_resolver_repository.name,
            OPEN_RIGHT_READABLE,
        )
        .await
        .unwrap();
        let repository_config_contents = read(&repository_config_file).await.unwrap();

        match from_slice::<RepositoryConfigs>(repository_config_contents.as_slice()).unwrap() {
            RepositoryConfigs::Version1(repository_configs) => {
                hostnames.extend(
                    repository_configs
                        .into_iter()
                        .map(|repository_config| repository_config.repo_url().host().to_string()),
                );
            }
        }
    }

    hostname_from_vec(&hostnames)
}
