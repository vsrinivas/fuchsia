// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Automatically launch the browser and retrieve the refresh token. This is in
//! contrast to the OOB auth where the user opens the browser and copies the
//! refresh token by hand.
//!
//! For this to function, the user must have an available GUI, thus the name.

use {
    crate::{
        auth::info::{CLIENT_ID, CLIENT_SECRET},
        error::GcsError,
    },
    anyhow::{bail, Context, Result},
    hyper::{Body, Method, Request},
    serde::{Deserialize, Serialize},
    serde_json,
    sha2::{Digest, Sha256},
    std::{
        io::{Read, Write},
        net::{Ipv4Addr, SocketAddr, TcpListener, TcpStream},
        string::String,
    },
    structured_ui,
    url::form_urlencoded,
};

/// URL used for gaining a new access token.
///
/// See RefreshTokenRequest, OauthTokenResponse.
const OAUTH_REFRESH_TOKEN_ENDPOINT: &str = "https://www.googleapis.com/oauth2/v3/token";

const AUTHORIZATION_ENDPOINT: &str = "https://accounts.google.com/o/oauth2/v2/auth";

/// URL used to exchange an auth_code for a refresh_token.
const EXCHANGE_AUTH_CODE_URL: &str = "https://oauth2.googleapis.com/token";

const PKCE_BYTE_LENGTH: usize = 32;

/// POST body to [`EXCHANGE_AUTH_CODE_URL`].
#[derive(Serialize)]
struct ExchangeAuthCodeRequest<'a> {
    /// A value provided by GCS for fetching tokens.
    client_id: &'a str,

    /// A (normally secret) value provided by GCS for fetching tokens.
    client_secret: &'a str,

    /// A short lived authorization code used to attain an initial
    /// `refresh_token` and `access_token`.
    code: &'a str,

    /// Will be "authorization_code" for a authorization code.
    grant_type: &'a str,

    /// A local loopback uri to receive the response (with the auth code).
    redirect_uri: &'a str,
}

/// Response body from [`EXCHANGE_AUTH_CODE_URL`].
/// 'expires_in' is intentionally omitted.
#[derive(Deserialize)]
struct ExchangeAuthCodeResponse {
    /// A limited time (see `expires_in`) token used in an Authorization header.
    access_token: Option<String>,

    /// A long lasting secret token. This value is a user secret and must not be
    /// misused (such as by logging). Suitable for storing in a local file and
    /// reusing later.
    refresh_token: String,
}

/// Response body from [`OAUTH_REFRESH_TOKEN_ENDPOINT`].
/// 'expires_in' is intentionally omitted.
#[derive(Deserialize)]
struct OauthTokenResponse {
    /// A limited time (see `expires_in`) token used in an Authorization header.
    access_token: String,
}

/// POST body to [`OAUTH_REFRESH_TOKEN_ENDPOINT`].
#[derive(Serialize)]
struct RefreshTokenRequest<'a> {
    /// A value provided by GCS for fetching tokens.
    client_id: &'a str,

    /// A (normally secret) value provided by GCS for fetching tokens.
    client_secret: &'a str,

    /// A long lasting secret token used to attain a new `access_token`.
    refresh_token: &'a str,

    /// Will be "refresh_token" for a refresh token.
    grant_type: &'a str,
}

/// Performs steps to get a refresh token from scratch.
///
/// This may involve user interaction such as opening a browser window..
pub async fn new_refresh_token<I>(_ui: &I) -> Result<String>
where
    I: structured_ui::Interface + Sync,
{
    let (auth_code, code_verifier, redirect_uri) =
        get_auth_code().await.context("getting auth code")?;
    let (refresh_token, _) = auth_code_to_refresh(&auth_code, &code_verifier, &redirect_uri)
        .await
        .context("getting new refresh token")?;
    Ok(refresh_token)
}

/// Use the 'refresh_token' to request an access token.
///
/// Access tokens are short-lived. Unlike a refresh token, there's little value
/// in storing an access token to disk for later use, though it may be used many
/// times before needing to get a new access_token.
pub async fn new_access_token(refresh_token: &str) -> Result<String, GcsError> {
    tracing::debug!("new_access_token");
    let req_body = RefreshTokenRequest {
        client_id: CLIENT_ID,
        client_secret: CLIENT_SECRET,
        refresh_token: refresh_token,
        grant_type: "refresh_token",
    };
    let body = serde_json::to_vec(&req_body)?;
    let req = Request::builder()
        .method(Method::POST)
        .uri(OAUTH_REFRESH_TOKEN_ENDPOINT)
        .body(Body::from(body))?;

    let https_client = fuchsia_hyper::new_https_client();
    let res = https_client.request(req).await?;

    if res.status().is_success() {
        let bytes = hyper::body::to_bytes(res.into_body()).await?;
        let info: OauthTokenResponse = serde_json::from_slice(&bytes)?;
        Ok(info.access_token)
    } else {
        match res.status() {
            http::StatusCode::BAD_REQUEST => return Err(GcsError::NeedNewRefreshToken),
            _ => return Err(GcsError::HttpResponseError(res.status())),
        }
    }
}

pub(crate) struct AuthCode(pub String);
pub(crate) struct CodeVerifier(pub String);
pub(crate) struct EncodedRedirect(pub String);

/// Ask the user to approve auth.
///
/// Returns (auth_code, code_verifier).
async fn get_auth_code() -> Result<(AuthCode, CodeVerifier, EncodedRedirect)> {
    tracing::debug!("get_auth_code");
    let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);
    let listener = TcpListener::bind(&addr).context("TcpListener::bind")?;

    // Generate state and PKCE values.
    let state = random_base64_url_encoded(PKCE_BYTE_LENGTH);
    let code_verifier = random_base64_url_encoded(PKCE_BYTE_LENGTH);
    let code_challenge = base64_url(&Sha256::digest(&code_verifier.as_bytes()));

    // Local loopback URL.
    let local_addr = listener.local_addr().context("getting local address")?;
    let redirect_uri = format!("http://{}/", local_addr);
    tracing::info!("redirect URI: {:?}", redirect_uri);
    let encoded_redirect = form_urlencoded::Serializer::new(redirect_uri).finish();

    // OAuth2 URL.
    let authorization_request = format!(
        "{}\
        ?response_type=code\
        &scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcloud-platform\
        &redirect_uri={}&client_id={}&state={}&code_challenge={}\
        &code_challenge_method=S256",
        AUTHORIZATION_ENDPOINT, encoded_redirect, CLIENT_ID, state, code_challenge,
    );

    // Simple background listener.
    let handle = std::thread::spawn(move || {
        tracing::debug!("OAuth2: listening for local connection.");
        let mut incoming = listener.incoming();
        match incoming.next() {
            Some(Ok(stream)) => {
                return Ok(handle_connection(stream, &state).context("handling connection")?);
            }
            Some(Err(e)) => {
                bail!("Connection failed {:?}", e);
            }
            None => {
                bail!("no incoming stream");
            }
        }
    });

    browser_open(&authorization_request)?;

    tracing::debug!("Joining background thread");
    let auth_code = handle.join().expect("handling thread join")?;
    tracing::info!("HTTP server stopped.");

    assert!(!auth_code.is_empty(), "auth_code must not be empty");
    assert!(!code_verifier.is_empty(), "code_verifier must not be empty");
    tracing::debug!("get_auth_code success");
    Ok((AuthCode(auth_code), CodeVerifier(code_verifier), EncodedRedirect(encoded_redirect)))
}

fn handle_connection(mut stream: TcpStream, state: &str) -> Result<String> {
    tracing::debug!("handle_connection");
    // More than enough for the expected message.
    const BUF_LEN: usize = 8 * 1024;
    let mut data = [0; BUF_LEN];
    let length = stream.read(&mut data).context("OAuth2 read from stream.")?;
    if length >= BUF_LEN {
        // There's only one expected client and they're only expected
        // to send one message. If a different message is received then
        // something may be trying to get in the middle.
        panic!(
            "The oauth2 message ({} bytes) received exceeded the \
                    expected size. This is suspicious.",
            length
        );
    }
    let uri = url_from_buf(&data[..length])?;
    let mut incoming_state = "".to_string();
    let mut auth_code = "".to_string();
    tracing::debug!("Scanning for code and state");
    for (key, value) in uri.query_pairs() {
        match &key {
            std::borrow::Cow::Borrowed("code") => auth_code = value.to_string(),
            std::borrow::Cow::Borrowed("state") => incoming_state = value.to_string(),
            _ => (),
        }
    }
    if incoming_state != state {
        tracing::debug!("Incoming state mismatch");
        let response_string = "\
                HTTP/1.1 400 Bad Request\r\n\
                \r\n\
                <html><head>\
                </head><body>\
                Received request with invalid state. Please try running the \
                command again.\
                </body></html>\r\n\r\n";
        stream.write_all(&response_string.as_bytes()).context("writing response to auth")?;
        stream.flush().context("flushing response stream")?;
        bail!(
            "OAuth2: Received request with invalid state {:?}. \
            Please try running the command again.",
            incoming_state
        )
    }

    tracing::debug!("Sending response page");
    let response_string = "\
            HTTP/1.1 200 OK\r\n\
            \r\n\
            <html><head>\
            </head><body>\
            Permission granted, please return to the terminal where ffx is \
            running.\
            <p>(It's okay to close this web page now.)
            </body></html>\r\n\r\n";
    stream.write_all(&response_string.as_bytes()).context("writing response to auth")?;
    stream.flush().context("flushing response stream")?;

    tracing::debug!("Got auth code");
    Ok(auth_code)
}

/// Convert a byte array to a URL.
fn url_from_buf(data: &[u8]) -> Result<url::Url> {
    tracing::debug!("url_from_buf");
    let message = std::str::from_utf8(data).context("OAuth2 convert to utf8")?;
    let uri_start = message.find(" ").context("OAuth2 find first space")? + 1;
    let uri_end = message[uri_start..].find(" ").context("OAuth2 find second space")?;
    // An unused scheme and domain are added to satisfy the parser.
    url::Url::parse(&format!("http://unused/{}", &message[uri_start..uri_start + uri_end]))
        .context("OAuth2 pars url")
}

/// Open the given 'url' in the default browser.
fn browser_open(url: &str) -> Result<()> {
    use std::process::{Command, Stdio};
    tracing::info!("browser_open");

    #[cfg(not(any(target_os = "linux", target_os = "macos")))]
    bail!(
        "This target_os is not supported for opening a browser \
        automatically. Please file a bug to request support."
    );

    println!(
        "A browser window will open to request access to GCS. \
        Please grant access.\n\nIf you're running without a gui (such as \
        through ssh) this may appear to hang,\nin that case rerun with the \
        `--auth oob` option.\n\n(If you decide not to grant access, press \
        ctrl+c to exit.)\n"
    );

    #[cfg(target_os = "linux")]
    Command::new("xdg-open")
        .arg(url)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()?;

    #[cfg(target_os = "macos")]
    Command::new("/usr/bin/open")
        .arg(url)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()?;

    tracing::debug!("opened browser");
    Ok(())
}

/// Convert an oauth2 authorization code to a refresh token.
///
/// The `auth_code` must not be an empty string (this will generate an Err
/// Result.
pub(crate) async fn auth_code_to_refresh(
    auth_code: &AuthCode,
    code_verifier: &CodeVerifier,
    redirect_uri: &EncodedRedirect,
) -> Result<(String, Option<String>), GcsError> {
    tracing::debug!("auth_code_to_refresh");
    assert!(!auth_code.0.is_empty(), "The auth code must not be empty");

    // Add POST parameters to exchange the auth_code for a refresh_token
    // and possibly an access_token.
    let body = form_urlencoded::Serializer::new(String::new())
        .append_pair("code", &auth_code.0)
        .append_pair("code_verifier", &code_verifier.0)
        .append_pair("redirect_uri", &redirect_uri.0)
        .append_pair("client_id", CLIENT_ID)
        .append_pair("client_secret", CLIENT_SECRET)
        .append_pair("scope", "")
        .append_pair("grant_type", "authorization_code")
        .finish();
    // Build the request and send it.
    let req = Request::builder()
        .method(Method::POST)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .uri(EXCHANGE_AUTH_CODE_URL)
        .body(Body::from(body))?;

    let https_client = fuchsia_hyper::new_https_client();
    let res = https_client.request(req).await?;

    if !res.status().is_success() {
        return Err(GcsError::RefreshAccessError(res.status()));
    }

    // Extract the new tokens.
    let bytes = hyper::body::to_bytes(res.into_body()).await?;
    let info: ExchangeAuthCodeResponse = serde_json::from_slice(&bytes)?;
    let refresh_token = info.refresh_token;
    let access_token = info.access_token;
    Ok((refresh_token.to_string(), access_token))
}

/// Create a random number expressed as a url safe string of base64.
///
/// Similar to a private key of 'count' bytes long.
fn random_base64_url_encoded(count: usize) -> String {
    use rand::rngs::StdRng;
    use rand::{Rng, SeedableRng};
    let mut rng = StdRng::from_entropy();
    let mut value = vec![0u8; count];
    rng.fill(&mut *value);
    base64_url(&value)
}

/// Encode 'buf' in base64 with no padding characters.
///
/// See also https://datatracker.ietf.org/doc/html/rfc4648#section-5
fn base64_url(buf: &[u8]) -> String {
    base64::encode_config(buf, base64::URL_SAFE_NO_PAD)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_random_base64_url_encoded() {
        // While it is possible to get the same number twice in a row, it's a
        // reasonable assertion to make. Unless something is wrong, it should
        // happen less than a cosmic ray bit-flip.
        assert!(
            random_base64_url_encoded(PKCE_BYTE_LENGTH)
                != random_base64_url_encoded(PKCE_BYTE_LENGTH)
        );
    }

    #[test]
    fn test_base64_url() {
        // No modifications.
        assert_eq!(base64_url(b"abc"), "YWJj".to_string());
        // Normally includes trailing "==".
        assert_eq!(base64_url(b"abcd"), "YWJjZA".to_string());
    }
}
