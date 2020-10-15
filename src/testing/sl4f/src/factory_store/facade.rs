// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

use crate::factory_store::types::{FactoryStoreProvider, ListFilesRequest, ReadFileRequest};

use base64;
use fidl::endpoints::create_proxy;
use fidl_fuchsia_factory::{
    AlphaFactoryStoreProviderMarker, CastCredentialsFactoryStoreProviderMarker,
    MiscFactoryStoreProviderMarker, PlayReadyFactoryStoreProviderMarker,
    WeaveFactoryStoreProviderMarker, WidevineFactoryStoreProviderMarker,
};
use fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, OPEN_RIGHT_READABLE};
use files_async::{readdir_recursive, DirentKind};
use fuchsia_component::client::connect_to_service;
use futures::stream::TryStreamExt;
use io_util;
use serde_json::{from_value, to_value, Value};
use std::path::Path;

/// Facade providing access to FactoryStoreProvider interfaces.
#[derive(Debug)]
pub struct FactoryStoreFacade;
impl FactoryStoreFacade {
    pub fn new() -> Self {
        FactoryStoreFacade {}
    }

    /// Lists the files from a given provider.
    ///
    /// # Arguments
    /// * `args`: A serde_json Value with the following format:
    /// ```json
    /// {
    ///   "provider": string
    /// }
    /// ```
    ///
    /// The provider string is expected to be a value from
    /// `types::FactoryStoreProvider`.
    pub async fn list_files(&self, args: Value) -> Result<Value, Error> {
        let req: ListFilesRequest = from_value(args)?;
        let dir_proxy = self.get_directory_for_provider(req.provider)?;

        let mut file_paths = Vec::new();
        let mut stream = readdir_recursive(&dir_proxy, /*timeout=*/ None);
        while let Some(entry) = stream.try_next().await? {
            if entry.kind == DirentKind::File {
                file_paths.push(entry.name);
            }
        }

        Ok(to_value(file_paths)?)
    }

    /// Reads a file from the given provider.
    ///
    /// # Arguments
    /// * `args`: A serde_json Value with the following format:
    ///
    /// ```json
    /// {
    ///   "provider": string,
    ///   "filename": string
    /// }
    /// ```
    ///
    /// The provider string is expected to match the serialized string of a
    /// value from `types::FactoryStoreProvider`. The filename string is
    /// expected to be a relative file path.
    pub async fn read_file(&self, args: Value) -> Result<Value, Error> {
        let req: ReadFileRequest = from_value(args)?;
        let dir_proxy = self.get_directory_for_provider(req.provider)?;

        let file = io_util::open_file(&dir_proxy, &Path::new(&req.filename), OPEN_RIGHT_READABLE)?;
        let contents = io_util::read_file_bytes(&file).await?;
        Ok(to_value(base64::encode(&contents))?)
    }

    /// Gets a `DirectoryProxy` that is connected to the given `provider`.
    ///
    /// # Arguments
    /// * `provider`: The factory store provider that the directory connects to.
    fn get_directory_for_provider(
        &self,
        provider: FactoryStoreProvider,
    ) -> Result<DirectoryProxy, Error> {
        let (dir_proxy, dir_server_end) = create_proxy::<DirectoryMarker>()?;

        match provider {
            FactoryStoreProvider::Alpha => {
                let alpha_svc = connect_to_service::<AlphaFactoryStoreProviderMarker>()?;
                alpha_svc.get_factory_store(dir_server_end)?;
            }
            FactoryStoreProvider::Cast => {
                let cast_svc = connect_to_service::<CastCredentialsFactoryStoreProviderMarker>()?;
                cast_svc.get_factory_store(dir_server_end)?;
            }
            FactoryStoreProvider::Misc => {
                let misc_svc = connect_to_service::<MiscFactoryStoreProviderMarker>()?;
                misc_svc.get_factory_store(dir_server_end)?;
            }
            FactoryStoreProvider::Playready => {
                let playready_svc = connect_to_service::<PlayReadyFactoryStoreProviderMarker>()?;
                playready_svc.get_factory_store(dir_server_end)?;
            }
            FactoryStoreProvider::Weave => {
                let weave_svc = connect_to_service::<WeaveFactoryStoreProviderMarker>()?;
                weave_svc.get_factory_store(dir_server_end)?;
            }
            FactoryStoreProvider::Widevine => {
                let widevine_svc = connect_to_service::<WidevineFactoryStoreProviderMarker>()?;
                widevine_svc.get_factory_store(dir_server_end)?;
            }
        }

        Ok(dir_proxy)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use lazy_static::lazy_static;
    use maplit::hashmap;
    use serde_json::json;
    use std::collections::HashMap;

    lazy_static! {
        static ref GOLDEN_FILE_DATA: HashMap<&'static str, HashMap<&'static str, &'static str>> = hashmap! {
            "alpha" => hashmap! {
                "alpha.file" => "alpha info",
                "alpha/data" => "alpha data"
            },
            "cast" => hashmap! {
                "txt/info.txt" => "cast info.txt",
                "more.extra" => "extra cast stuff",
            },
            "misc" => hashmap! {
                "info/misc" => "misc.info",
                "more.misc" => "more misc stuff"
            },
            "playready" => hashmap! {
                "pr/pr/prinfo.dat" => "playready info",
                "dat.stuff" => "playready stuff"
            },
            "weave" => hashmap! {
                "weave.file" => "weave info",
                "weave/data" => "weave data"
            },
            "widevine" => hashmap! {
                "stuff.log" => "widevine stuff",
                "wv/more_stuff" => "more_stuff from widevine",
            }
        };
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_files_with_no_message_fails() -> Result<(), Error> {
        let factory_store_facade = FactoryStoreFacade::new();
        factory_store_facade.list_files(json!("")).await.unwrap_err();
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_files_with_unknown_provider_fails() -> Result<(), Error> {
        let factory_store_facade = FactoryStoreFacade::new();
        factory_store_facade.list_files(json!({ "provider": "unknown" })).await.unwrap_err();
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_files() -> Result<(), Error> {
        let factory_store_facade = FactoryStoreFacade::new();

        for (provider, file_map) in GOLDEN_FILE_DATA.iter() {
            let file_list_json_value =
                factory_store_facade.list_files(json!({ "provider": provider })).await?;

            let mut file_list: Vec<String> = from_value(file_list_json_value)?;
            let mut expected_file_list: Vec<&str> =
                file_map.keys().map(|entry| entry.clone()).collect();

            expected_file_list.sort();
            file_list.sort();
            assert_eq!(expected_file_list, file_list);
        }

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_file_with_unknown_provider_fails() -> Result<(), Error> {
        let factory_store_facade = FactoryStoreFacade::new();
        factory_store_facade
            .read_file(json!({ "provider": "unknown", "filename": "missing_file"  }))
            .await
            .unwrap_err();
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_file_with_unknown_file_fails() -> Result<(), Error> {
        let factory_store_facade = FactoryStoreFacade::new();
        factory_store_facade
            .read_file(json!({ "provider": "cast", "filename": "missing_file"  }))
            .await
            .unwrap_err();
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_files() -> Result<(), Error> {
        let factory_store_facade = FactoryStoreFacade::new();

        for (provider, file_map) in GOLDEN_FILE_DATA.iter() {
            for (filename, expected_contents) in file_map.iter() {
                let contents_value = factory_store_facade
                    .read_file(json!({ "provider": provider, "filename": filename }))
                    .await?;
                let contents_base64: String = from_value(contents_value)?;
                let contents = base64::decode(contents_base64.as_bytes())?;
                assert_eq!(expected_contents.as_bytes(), &contents[..]);
            }
        }
        Ok(())
    }
}
