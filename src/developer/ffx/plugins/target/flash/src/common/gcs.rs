// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Resolves paths found in a flashing manifest by downloading files from GCS and storing them in a
//! temporary directory for the flashing algorithm to use. Files are only downloaded on demand so
//! all files from a flashing manifest are not necessarily downloaded - only the ones needed.

use {
    crate::common::{done_time, file::FileResolver},
    anyhow::{anyhow, bail, Result},
    async_trait::async_trait,
    chrono::Utc,
    errors::ffx_bail,
    gcs::{
        client::{Client, ClientFactory},
        token_store::{read_boto_refresh_token, TokenStore},
    },
    sdk_metadata::{ElementType, Envelope, ProductBundleContainerV1, ProductBundleV1},
    std::fs::File,
    std::io::{BufReader, Write},
    std::path::{Path, PathBuf},
    tempfile::{tempdir, TempDir},
};

// Path resolver for GCS files.
pub(crate) struct GcsResolver {
    client: Client,
    product_bundle: ProductBundleV1,
    temp_dir: TempDir,
}

impl GcsResolver {
    pub(crate) async fn new(version: String, bundle: String) -> Result<Self> {
        let temp_dir = tempdir()?;
        // TODO(fxb/89584): Change to using ffx client Id and consent screen.
        let boto: Option<PathBuf> = ffx_config::file("flash.gcs.token").await?;
        let auth = match boto {
            Some(boto_path) => {
                TokenStore::new_with_auth(
                    read_boto_refresh_token(&boto_path)?
                        .ok_or(anyhow!("Could not read boto token store"))?,
                    /*access_token=*/ None,
                )?
            }
            None => ffx_bail!("GCS authentication not found."),
        };

        let client_factory = ClientFactory::new(auth);
        let client = client_factory.create_client();

        let product_bundle_container_path = temp_dir.path().join("product_bundles.json");
        let gcs_path = format!("development/{}/sdk/product_bundles.json", version);
        client.fetch("fuchsia-sdk", &gcs_path, &product_bundle_container_path).await?;

        let product_bundle =
            product_bundle_from_container_path(&product_bundle_container_path, &bundle)?;

        Ok(Self { client, product_bundle, temp_dir })
    }

    pub(crate) fn product_bundle(&self) -> &ProductBundleV1 {
        &self.product_bundle
    }
}

fn product_bundle_from_container_path<P: AsRef<Path>>(
    path: P,
    bundle: &String,
) -> Result<ProductBundleV1> {
    let container: Envelope<ProductBundleContainerV1> =
        File::open(path).map(BufReader::new).map(serde_json::from_reader)??;
    let product_bundle = container
        .data
        .bundles
        .iter()
        .find(|b| match b.data.kind {
            ElementType::ProductBundle => b.data.name == *bundle,
            _ => false,
        })
        .map(|b| b.data.clone());
    product_bundle.map(|pb| pb).ok_or_else(|| anyhow!("No matching bundle found."))
}

#[async_trait(?Send)]
impl FileResolver for GcsResolver {
    fn manifest(&self) -> &Path {
        // This is not used in the get_file method so it's not needed.
        unimplemented!()
    }

    async fn get_file<W: Write>(&mut self, writer: &mut W, file: &str) -> Result<String> {
        match self
            .product_bundle()
            .images
            .iter()
            .find_map(|i| extract_project_and_path(&i.base_uri))
        {
            Some((project, path)) => {
                let gcs_path = format!("{}/{}", path, file);
                let download = Utc::now();
                write!(writer, "Downloading {}/{}... ", project, gcs_path)?;
                writer.flush()?;
                let path = self.temp_dir.path().join(file);
                if let Some(p) = path.parent() {
                    std::fs::create_dir_all(&p)?;
                }
                self.client.fetch(&project, &gcs_path, &path).await?;
                let d = Utc::now().signed_duration_since(download);
                done_time(writer, d)?;
                path.to_str()
                    .map(|s| s.to_string())
                    .ok_or(anyhow!("Could not formulate path: {}", path.display()))
            }
            None => {
                bail!("Could not formulate GCS path for {}", file);
            }
        }
    }
}

fn extract_project_and_path(url: &String) -> Option<(String, String)> {
    url::Url::parse(url)
        .ok()
        .and_then(|u| u.host_str().map(|h| (h.to_string(), u.path().to_string())))
}
