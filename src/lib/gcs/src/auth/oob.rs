// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Out of Band (OOB) auth flow. This flow has been deprecated by Google. It
//! will stop functioning sometime in January 2022.

use {
    crate::auth::{
        info::CLIENT_ID,
        pkce::{auth_code_to_refresh, AuthCode, CodeVerifier, EncodedRedirect},
    },
    anyhow::{Context, Result},
    std::{
        io::{self, BufRead, BufReader, Read, Write},
        string::String,
    },
};

const OOB_APPROVE_AUTH_CODE_URL: &str = "\
        https://accounts.google.com/o/oauth2/auth?\
scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcloud-platform\
&redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob\
&response_type=code\
&access_type=offline\
&client_id=";

/// Performs steps to get a refresh token from scratch.
///
/// This may involve user interaction such as opening a browser window..
pub async fn new_refresh_token() -> Result<String> {
    tracing::debug!("oob_new_refresh_token");
    let auth_code = AuthCode(oob_get_auth_code().context("getting auth code")?);
    let verifier = CodeVerifier("".to_string());
    let redirect = EncodedRedirect("urn:ietf:wg:oauth:2.0:oob".to_string());
    let (refresh_token, _) = auth_code_to_refresh(&auth_code, &verifier, &redirect)
        .await
        .context("get refresh token")?;
    Ok(refresh_token)
}

/// Ask the user to visit a URL and copy-paste the auth code provided.
///
/// A helper wrapper around get_auth_code_with() using stdin/stdout.
fn oob_get_auth_code() -> Result<String> {
    tracing::debug!("oob_get_auth_code");
    let stdout = io::stdout();
    let mut output = stdout.lock();
    let stdin = io::stdin();
    let mut input = stdin.lock();
    oob_get_auth_code_with(&mut output, &mut input)
}

/// Ask the user to visit a URL and copy-paste the authorization code provided.
/// Consider using `oob_get_auth_code()` for operation with stdin/stdout.
///
/// For a GUI, consider creating a separate (custom) function to ask the user to
/// follow the web flow to get a authorization code (tip: use `auth_code_url()`
/// to get the URL).
fn oob_get_auth_code_with<W, R>(writer: &mut W, reader: &mut R) -> Result<String>
where
    W: Write,
    R: Read,
{
    tracing::debug!("oob_get_auth_code_with");
    writeln!(
        writer,
        "Please visit this site. Proceed through the web flow to allow access \
        and copy the authentication code:\
        \n\n{}{}\n\nPaste the code (from the web page) here\
        \nand press return: ",
        OOB_APPROVE_AUTH_CODE_URL, CLIENT_ID,
    )?;
    writer.flush().expect("flush auth code prompt");
    let mut auth_code = String::new();
    let mut buf_reader = BufReader::new(reader);
    buf_reader.read_line(&mut auth_code).expect("Need an auth_code.");
    Ok(auth_code.trim().to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_oob_get_auth_code_with() {
        let mut output: Vec<u8> = Vec::new();
        let mut input = "fake_auth_code".as_bytes();
        let auth_code = oob_get_auth_code_with(&mut output, &mut input).expect("auth code");
        assert_eq!(auth_code, "fake_auth_code");
    }
}
