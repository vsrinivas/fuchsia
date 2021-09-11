// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool for downloading data.
//! (Think `gsutil` with fewer options, but has support for Fuchsia specific
//! needs.)

use {anyhow::Result, args::Args, gcs::token_store::auth_code_url};

mod args;

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<()> {
    println!("\nWIP download tool -- This isn't useful yet, stay tuned.\n\n");
    let args: Args = argh::from_env();
    // Print args get auth just do something in this WIP version of the tool.
    println!("args {:?}", args);
    let _ = get_auth_code();
    Ok(())
}

/// Ask the user to visit a URL and copy-paste the auth code provided.
fn get_auth_code() -> String {
    use std::io::{stdin, stdout, Write};
    print!(
        "Please visit this site and copy the authentication code:\
        \n\n{}\n\nPaste the auth_code (from website) here and press return: ",
        auth_code_url(),
    );
    stdout().flush().expect("stdout flush");
    let mut auth_code = String::new();
    stdin().read_line(&mut auth_code).expect("Need an auth_code.");
    auth_code
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_hyper::new_https_client,
        gcs::{client::ClientFactory, token_store::TokenStore},
    };

    async fn gcs_download() -> Result<()> {
        let auth_code = get_auth_code();
        let token_store =
            TokenStore::new_with_code(&new_https_client(), &auth_code).await.expect("token_store");

        let factory = ClientFactory::new(token_store);
        let client = factory.create_client();

        let bucket = "fuchsia-sdk";
        let object = "development/LATEST_LINUX";
        let res = client.stream(bucket, object).await.expect("client download");
        assert_eq!(res.status(), 200, "res {:?}", res);
        Ok(())
    }

    async fn https_download() -> Result<()> {
        use hyper::{body::HttpBody, Body, Method, Request, Response, StatusCode};
        use std::io::{self, Write};
        println!("hyper_test");

        let https_client = new_https_client();
        let req = Request::builder().method(Method::GET).uri("https://www.google.com/");
        let req = req.body(Body::from(""))?;
        let mut res: Response<Body> = https_client.request(req).await?;
        if res.status() == StatusCode::OK {
            let stdout = io::stdout();
            let mut handle = stdout.lock();
            while let Some(next) = res.data().await {
                let chunk = next?;
                handle.write_all(&chunk)?;
            }
        }
        Ok(())
    }

    /// This test relies on a local file which is not present on test bots, so
    /// it is marked "ignore".
    /// This can be run with `fx test fgsutil_test -- --ignored`.
    #[ignore]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_gcs_download() {
        gcs_download().await.expect("gcs download");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_https_download() {
        https_download().await.expect("https download from google.com");
    }
}
