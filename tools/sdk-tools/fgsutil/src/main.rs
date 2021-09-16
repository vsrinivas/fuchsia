// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool for downloading data.
//! (Think `gsutil` with fewer options, but has support for Fuchsia specific
//! needs.)

use {
    crate::config::{read_config, write_config, Configuration},
    anyhow::{anyhow, bail, Result},
    args::{Args, SubCommand},
    fuchsia_hyper::new_https_client,
    gcs::token_store::{auth_code_url, TokenStore},
    home::home_dir,
    std::{
        io::{self, BufRead, BufReader, Read, Write},
        path::{Path, PathBuf},
    },
};

mod args;
mod config;

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<()> {
    println!("\nWIP download tool -- This isn't useful yet, stay tuned.\n\n");
    let args: Args = argh::from_env();
    main_helper(args).await
}

/// A wrapper for the main program flow (allows for easier testing).
async fn main_helper(args: Args) -> Result<()> {
    let config_path = build_config_path()?;
    match args.cmd {
        SubCommand::Cat(_) => cat_command(&config_path).await,
        SubCommand::Config(_) => config_command(&config_path).await,
        SubCommand::Cp(_) => cp_command(&config_path).await,
        SubCommand::List(_) => list_command(&config_path).await,
    }
}

/// Determine where the configuration file should be.
fn build_config_path() -> Result<PathBuf> {
    let home = home_dir().ok_or(anyhow!("Unable to find home directory."))?;
    let dir = home.join(".fuchsia").join("fgsutil");
    std::fs::create_dir_all(&dir)?;
    Ok(dir.join("config.json"))
}

/// Handle the `cat` (concatenate) command and its args.
async fn cat_command(config_path: &Path) -> Result<()> {
    let _config = read_config(&config_path).await?;
    println!("The cat command is a WIP. Nothing here yet.");
    Ok(())
}

/// Handle the `config` (configuration/set up) command and its args.
async fn config_command(config_path: &Path) -> Result<()> {
    let stdout = io::stdout();
    let mut output = stdout.lock();
    let stdin = io::stdin();
    let mut input = stdin.lock();
    let auth_code_ = get_auth_code(&mut output, &mut input)?;

    let refresh_token = auth_code_to_refresh(&auth_code_).await?;
    let mut config = Configuration::default();
    config.gcs.refresh_token = Some(refresh_token.to_owned());

    write_config(&config, &config_path).await?;
    Ok(())
}

/// Convert an authorization code to a refresh token.
async fn auth_code_to_refresh(auth_code: &str) -> Result<String> {
    let token_store = TokenStore::new_with_code(&new_https_client(), auth_code).await?;
    match token_store.refresh_token() {
        Some(s) => Ok(s.to_string()),
        None => bail!("auth_code_to_refresh failed"),
    }
}

/// Handle the `cp` (copy) command and its args.
async fn cp_command(config_path: &Path) -> Result<()> {
    let _config = read_config(&config_path).await?;
    println!("The cp command is a WIP. Nothing here yet.");
    Ok(())
}

/// Handle the `ls` (list) command and its args.
async fn list_command(config_path: &Path) -> Result<()> {
    let _config = read_config(&config_path).await?;
    println!("The list command is a WIP. Nothing here yet.");
    Ok(())
}

/// Ask the user to visit a URL and copy-paste the auth code provided.
fn get_auth_code<W, R>(writer: &mut W, reader: &mut R) -> Result<String>
where
    W: Write,
    R: Read,
{
    writeln!(
        writer,
        "Please visit this site and copy the authentication code:\
        \n\n{}\n\nPaste the auth_code (from website) here and press return: ",
        auth_code_url(),
    )?;
    writer.flush().expect("flush auth code prompt");
    let mut auth_code = String::new();
    let mut buf_reader = BufReader::new(reader);
    buf_reader.read_line(&mut auth_code).expect("Need an auth_code.");
    Ok(auth_code)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        gcs::{client::ClientFactory, token_store::TokenStore},
    };

    async fn gcs_download() -> Result<()> {
        let stdout = io::stdout();
        let mut output = stdout.lock();
        let stdin = io::stdin();
        let mut input = stdin.lock();
        let auth_code = get_auth_code(&mut output, &mut input)?;
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

    #[test]
    fn test_get_auth_code() {
        let mut output: Vec<u8> = Vec::new();
        let mut input = "fake_auth_code".as_bytes();
        let auth_code = get_auth_code(&mut output, &mut input).expect("auth code");
        assert_eq!(auth_code, "fake_auth_code");
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
