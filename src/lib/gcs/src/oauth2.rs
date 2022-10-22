// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provide OAuth2 support for Google Cloud Storage (GCS) access.
//!
//! There are two main APIs here:
//! - new_refresh_token() gets a long-lived, storable (to disk) token than can
//!                       be used to create new access tokens.
//! - new_access_token() accepts a refresh token and returns a reusable though
//!                      short-lived token that can be used to access data.
//!
//! Caution: Some data handled here are security access keys (tokens) and must
//!          not be logged (or traced) or otherwise put someplace where the
//!          secrecy could be compromised. I.e. watch out when adding/reviewing
//!          log::*, tracing::*, or `impl` of Display or Debug.

use {
    crate::error::GcsError,
    anyhow::{bail, Context, Result},
    hyper::{Body, Method, Request},
    serde::{Deserialize, Serialize},
    serde_json,
    sha2::{Digest, Sha256},
    std::{
        io::{self, BufRead, BufReader, Read, Write},
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

/// For a web site, a client secret is kept locked away in a secure server. This
/// is not a web site and the value is needed, so a non-secret "secret" is used.
///
/// These values (and the quote following) are taken form
/// https://chromium.googlesource.com/chromium/tools/depot_tools.git/+/c6a2ee693093926868170f678d8d290bf0de0c15/third_party/gsutil/oauth2_plugin/oauth2_helper.py
/// and
/// https://chromium.googlesource.com/catapult/+/2c541cdf008959bc9813c641cc4ecd0194979486/third_party/gsutil/gslib/utils/system_util.py#177
///
/// "Google OAuth2 clients always have a secret, even if the client is an
/// installed application/utility such as gsutil.  Of course, in such cases the
/// "secret" is actually publicly known; security depends entirely on the
/// secrecy of refresh tokens, which effectively become bearer tokens."
const GSUTIL_CLIENT_ID: &str = "909320924072.apps.googleusercontent.com";
const GSUTIL_CLIENT_SECRET: &str = "p3RlpR10xMFh9ZXBS/ZNLYUu";

const OOB_APPROVE_AUTH_CODE_URL: &str = "\
        https://accounts.google.com/o/oauth2/auth?\
scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcloud-platform\
&redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob\
&response_type=code\
&access_type=offline\
&client_id=";

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
    tracing::trace!("new_access_token");
    let req_body = RefreshTokenRequest {
        client_id: GSUTIL_CLIENT_ID,
        client_secret: GSUTIL_CLIENT_SECRET,
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

struct AuthCode(String);
struct CodeVerifier(String);
struct EncodedRedirect(String);

/// Ask the user to approve auth.
///
/// Returns (auth_code, code_verifier).
async fn get_auth_code() -> Result<(AuthCode, CodeVerifier, EncodedRedirect)> {
    tracing::trace!("get_auth_code");
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
        AUTHORIZATION_ENDPOINT, encoded_redirect, GSUTIL_CLIENT_ID, state, code_challenge,
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

    tracing::trace!("Joining background thread");
    let auth_code = handle.join().expect("handling thread join")?;
    tracing::info!("HTTP server stopped.");

    assert!(!auth_code.is_empty(), "auth_code must not be empty");
    assert!(!code_verifier.is_empty(), "code_verifier must not be empty");
    tracing::trace!("get_auth_code success");
    Ok((AuthCode(auth_code), CodeVerifier(code_verifier), EncodedRedirect(encoded_redirect)))
}

fn handle_connection(mut stream: TcpStream, state: &str) -> Result<String> {
    tracing::trace!("handle_connection");
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
    tracing::trace!("Scanning for code and state");
    for (key, value) in uri.query_pairs() {
        match &key {
            std::borrow::Cow::Borrowed("code") => auth_code = value.to_string(),
            std::borrow::Cow::Borrowed("state") => incoming_state = value.to_string(),
            _ => (),
        }
    }
    if incoming_state != state {
        tracing::trace!("Incoming state mismatch");
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

    tracing::trace!("Sending response page");
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

    tracing::trace!("Got auth code");
    Ok(auth_code)
}

/// Convert a byte array to a URL.
fn url_from_buf(data: &[u8]) -> Result<url::Url> {
    tracing::trace!("url_from_buf");
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

    tracing::trace!("opened browser");
    Ok(())
}

/// Convert an oauth2 authorization code to a refresh token.
///
/// The `auth_code` must not be an empty string (this will generate an Err
/// Result.
async fn auth_code_to_refresh(
    auth_code: &AuthCode,
    code_verifier: &CodeVerifier,
    redirect_uri: &EncodedRedirect,
) -> Result<(String, Option<String>), GcsError> {
    tracing::trace!("auth_code_to_refresh");
    assert!(!auth_code.0.is_empty(), "The auth code must not be empty");

    // Add POST parameters to exchange the auth_code for a refresh_token
    // and possibly an access_token.
    let body = form_urlencoded::Serializer::new(String::new())
        .append_pair("code", &auth_code.0)
        .append_pair("code_verifier", &code_verifier.0)
        .append_pair("redirect_uri", &redirect_uri.0)
        .append_pair("client_id", GSUTIL_CLIENT_ID)
        .append_pair("client_secret", GSUTIL_CLIENT_SECRET)
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

/// Performs steps to get a refresh token from scratch.
///
/// This may involve user interaction such as opening a browser window..
pub async fn oob_new_refresh_token() -> Result<String> {
    tracing::trace!("oob_new_refresh_token");
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
    tracing::trace!("oob_get_auth_code");
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
    tracing::trace!("oob_get_auth_code_with");
    writeln!(
        writer,
        "Please visit this site. Proceed through the web flow to allow access \
        and copy the authentication code:\
        \n\n{}{}\n\nPaste the code (from the web page) here\
        \nand press return: ",
        OOB_APPROVE_AUTH_CODE_URL, GSUTIL_CLIENT_ID,
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
