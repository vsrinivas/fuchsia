// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Result};
use async_trait::async_trait;
use fuchsia_hyper::new_https_client;
use hyper::body::HttpBody;
use hyper::header::LOCATION;
use hyper::{StatusCode, Uri};
use mockall::automock;
use std::fs::{create_dir_all, set_permissions, File, Permissions};
use std::io::{copy, Write};
use std::path::PathBuf;
use zip::ZipArchive;

#[automock]
#[async_trait]
pub trait CipdMethods {
    async fn download(&self, url: Uri, dest: &PathBuf) -> Result<StatusCode>;
    fn extract_zip(&self, zip_file: &PathBuf, dest_root: &PathBuf, debug: bool) -> Result<()>;
}

pub struct Cipd {}
#[async_trait]
impl CipdMethods for Cipd {
    /// Downloads a package from CIPD. This handles 1 iteration of url re-direct.
    ///
    /// # Arguments
    ///
    /// * `url` - url to cipd package
    /// * `dest` - zip file location to save the fetched file.
    async fn download(&self, url: Uri, dest: &PathBuf) -> Result<StatusCode> {
        let client = new_https_client();
        println!("{}", format!("Downloading... from {} to {}", url.clone(), dest.display()));

        let mut res = client.get(url).await?;
        let mut status = res.status();
        if status == StatusCode::FOUND {
            let redirect_url = res
                .headers()
                .get(LOCATION)
                .ok_or(anyhow!("Cannot read http response header resp: {:?}", res))?;
            res = client.get(redirect_url.to_str()?.parse::<Uri>()?).await?;
            status = res.status();
        }
        if status == StatusCode::OK {
            let mut output = File::create(dest)?;
            while let Some(next) = res.data().await {
                let chunk = next?;
                output.write_all(&chunk)?;
            }
        }
        Ok(status)
    }

    /// Extracts a zip file to a specified location.
    ///
    /// # Arguments
    ///
    /// * `zip_file` - path to the zip file to extract
    /// * `dest_root` - path to the root location to extract the zip_file to
    fn extract_zip(&self, zip_file: &PathBuf, dest_root: &PathBuf, debug: bool) -> Result<()> {
        let file = File::open(&zip_file)?;
        let mut archive = ZipArchive::new(file)?;

        for i in 0..archive.len() {
            let mut file = archive.by_index(i)?;
            let out_path = dest_root.join(file.sanitized_name());

            if file.name().ends_with('/') {
                if debug {
                    println!("File {} extracted to \"{}\"", i, out_path.display());
                }
                create_dir_all(&out_path)?;
            } else {
                if debug {
                    println!(
                        "File {} extracted to \"{}\" ({} bytes)",
                        i,
                        out_path.display(),
                        file.size()
                    );
                }
                if let Some(p) = out_path.parent() {
                    if !p.exists() {
                        create_dir_all(&p)?;
                    }
                }
                let mut out_file = File::create(&out_path)?;
                copy(&mut file, &mut out_file)?;
            }

            // Get and Set permissions
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;

                if let Some(mode) = file.unix_mode() {
                    set_permissions(&out_path, Permissions::from_mode(mode))?;
                }
            }
        }
        Ok(())
    }
}
