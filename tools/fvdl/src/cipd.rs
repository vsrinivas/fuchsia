// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Result};
use fuchsia_hyper::new_https_client;
use hyper::body::HttpBody;
use hyper::header::LOCATION;
use hyper::{StatusCode, Uri};
use std::fs::{create_dir_all, set_permissions, File, Permissions};
use std::io::{copy, Write};
use std::path::PathBuf;
use zip::ZipArchive;

/// Downloads a package from CIPD. This handles 1 iteration of url re-direct.
///
/// # Arguments
///
/// * `url` - url to cipd package
/// * `dest` - zip file location to save the fetched file.
pub async fn download(url: Uri, dest: &PathBuf) -> Result<StatusCode> {
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
pub fn extract_zip(zip_file: &PathBuf, dest_root: &PathBuf) -> Result<()> {
    let file = File::open(&zip_file)?;
    let mut archive = ZipArchive::new(file)?;

    for i in 0..archive.len() {
        let mut file = archive.by_index(i)?;
        let outpath = dest_root.join(file.sanitized_name());

        if file.name().ends_with('/') {
            println!("File {} extracted to \"{}\"", i, outpath.display());
            create_dir_all(&outpath)?;
        } else {
            println!("File {} extracted to \"{}\" ({} bytes)", i, outpath.display(), file.size());
            if let Some(p) = outpath.parent() {
                if !p.exists() {
                    create_dir_all(&p)?;
                }
            }
            let mut outfile = File::create(&outpath)?;
            copy(&mut file, &mut outfile)?;
        }

        // Get and Set permissions
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;

            if let Some(mode) = file.unix_mode() {
                set_permissions(&outpath, Permissions::from_mode(mode))?;
            }
        }
    }
    Ok(())
}
