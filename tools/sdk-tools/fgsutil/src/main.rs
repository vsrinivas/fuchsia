// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool for downloading data.
//! (Think `gsutil` with fewer options, but has support for Fuchsia specific
//! needs.)

use {
    crate::config::{read_config, write_config, Configuration},
    anyhow::{anyhow, bail, Context, Result},
    args::{Args, CatArgs, ConfigArgs, CpArgs, ListArgs, SubCommand},
    fuchsia_hyper::new_https_client,
    gcs::{
        client::ClientFactory,
        gs_url::split_gs_url,
        token_store::{auth_code_url, TokenStore},
    },
    home::home_dir,
    std::{
        fs::OpenOptions,
        io::{self, BufRead, BufReader, Read, Write},
        path::{Path, PathBuf},
    },
};

mod args;
mod config;

const VERSION: &str = "0.3";

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<()> {
    let args: Args = argh::from_env();
    main_helper(args).await
}

/// A wrapper for the main program flow (allows for easier testing).
async fn main_helper(args: Args) -> Result<()> {
    if args.version {
        println!("Version {}", VERSION);
        return Ok(());
    }
    let config_path = build_config_path()?;
    match args.cmd {
        Some(cmd) => match cmd {
            SubCommand::Cat(args) => cat_command(&args, &config_path).await,
            SubCommand::Config(args) => config_command(&args, &config_path).await,
            SubCommand::Cp(args) => cp_command(&args, &config_path).await,
            SubCommand::List(args) => list_command(&args, &config_path).await,
        },
        None => bail!("Please enter a subcommand such as help, cat, config, cp, or list."),
    }
}

/// Helper function form common task of creating a client factory.
async fn create_client_factory(config_path: &Path) -> Result<ClientFactory> {
    let config = read_config(&config_path).await?;
    let refresh_token = config.gcs.require_refresh_token()?.to_string();
    let token_store = TokenStore::new_with_auth(refresh_token, /*access_token=*/ None)?;
    Ok(ClientFactory::new(token_store))
}

/// Determine where the configuration file should be.
fn build_config_path() -> Result<PathBuf> {
    let home = home_dir().ok_or(anyhow!("Unable to find home directory."))?;
    let dir = home.join(".fuchsia").join("fgsutil");
    std::fs::create_dir_all(&dir)?;
    Ok(dir.join("config.json"))
}

/// Handle the `cat` (concatenate) command and its args.
///
/// Loop over a list of gs URLs printing contents to stdout.
async fn cat_command(args: &CatArgs, config_path: &Path) -> Result<()> {
    if args.gs_url.is_empty() {
        bail!("One or more URLs are required. (e.g. gs://foo/bar)");
    }
    let factory = create_client_factory(config_path).await?;
    let client = factory.create_client();
    let stdout = io::stdout();
    let mut writer = stdout.lock();
    for split in args.gs_url.iter().map(split_gs_url) {
        let (bucket, object) = split?;
        client.write(bucket, object, &mut writer).await?;
    }
    Ok(())
}

/// Handle the `config` (configuration/set up) command and its args.
///
/// Prompt user for auth code and store the value in a configuration file.
async fn config_command(_args: &ConfigArgs, config_path: &Path) -> Result<()> {
    let auth_code = get_auth_code()?;
    let refresh_token = auth_code_to_refresh(&auth_code).await?;
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
///
/// Very limited version of gsutil cp: download one file only.
async fn cp_command(args: &CpArgs, config_path: &Path) -> Result<()> {
    let factory = create_client_factory(config_path).await?;
    let client = factory.create_client();

    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(true)
        .open(&args.destination)
        .context("create output file")?;
    let (bucket, object) = split_gs_url(&args.source)?;
    client.write(bucket, object, &mut file).await
}

/// Handle the `ls` (list) command and its args.
async fn list_command(args: &ListArgs, config_path: &Path) -> Result<()> {
    if args.gs_url.is_empty() {
        bail!("One or more URLs are required. (e.g. gs://foo/bar)");
    }
    let factory = create_client_factory(config_path).await?;
    let client = factory.create_client();
    let stdout = io::stdout();
    let mut writer = stdout.lock();
    for split in args.gs_url.iter().map(split_gs_url) {
        let (bucket, object_prefix) = split?;
        for item in client.list(bucket, object_prefix).await? {
            writeln!(&mut writer, "{}", item)?;
        }
    }
    Ok(())
}

/// Ask the user to visit a URL and copy-paste the auth code provided.
///
/// A helper wrapper around get_auth_code_with() using stdin/stdout.
fn get_auth_code() -> Result<String> {
    let stdout = io::stdout();
    let mut output = stdout.lock();
    let stdin = io::stdin();
    let mut input = stdin.lock();
    get_auth_code_with(&mut output, &mut input)
}

/// Ask the user to visit a URL and copy-paste the auth code provided.
fn get_auth_code_with<W, R>(writer: &mut W, reader: &mut R) -> Result<String>
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
    use super::*;

    async fn gcs_download() -> Result<()> {
        let auth_code = get_auth_code()?;
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
    fn test_get_auth_code_with() {
        let mut output: Vec<u8> = Vec::new();
        let mut input = "fake_auth_code".as_bytes();
        let auth_code = get_auth_code_with(&mut output, &mut input).expect("auth code");
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
