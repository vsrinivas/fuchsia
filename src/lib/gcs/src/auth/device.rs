// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Use the "device" auth flow. In this flow the user is asked to visit an auth
//! page on a separate machine and enter a short code.

use {
    crate::auth::info::{AUTH_SCOPE, CLIENT_ID, CLIENT_SECRET},
    anyhow::{bail, Context, Result},
    hyper::{Body, Method, Request},
    serde::{Deserialize, Serialize},
    serde_json,
    std::time::{Duration, Instant},
    structured_ui,
};

use cfg_if::cfg_if;
cfg_if! {
    if #[cfg(test)] {
        use crate::mock_https_client::{new_https_client, HttpsClient};
    } else {
        use fuchsia_hyper::HttpsClient;
        fn new_https_client() -> HttpsClient {
            fuchsia_hyper::new_https_client()
        }
    }
}

/// Request for a DeviceCodeResponse (or DeviceCodeError).
///
/// These fields are dictated by Google Identity.
/// https://developers.google.com/identity/protocols/oauth2/limited-input-device
#[derive(Debug, Serialize)]
struct DeviceCodeRequest<'a> {
    client_id: &'a str,
    scope: &'a str,
}

/// Response body from DeviceCodeRequest.
///
/// These fields are dictated by Google Identity.
#[derive(Debug, Deserialize)]
struct DeviceCodeResponse {
    /// For polling for results.
    device_code: String,

    /// Present to the user for use with `verification_url`.
    user_code: String,

    /// Present to the user.
    verification_url: String,

    /// In seconds.
    expires_in: u64,

    /// Please poll for results every N seconds.
    interval: u64,
}

/// Response body from DeviceCodeRequest.
///
/// These fields are dictated by Google Identity.
#[derive(Debug, Deserialize)]
struct DeviceCodeError {
    /// A brief error message.
    error: String,

    /// More error details.
    error_description: String,

    /// More info here.
    error_uri: String,
}

/// Request for a DeviceRefreshTokenResponse (or DeviceRefreshTokenError).
///
/// These fields are dictated by Google Identity.
#[derive(Debug, Serialize)]
struct DeviceRefreshTokenRequest<'a> {
    client_id: &'a str,
    client_secret: &'a str,
    code: &'a str,
    grant_type: &'a str,
}

/// Response body from DeviceRefreshTokenRequest.
///
/// These fields are dictated by Google Identity.
#[derive(Debug, Deserialize)]
struct DeviceRefreshTokenResponse {
    /// The following fields are available in the message, but are unused in
    /// this code. Feel free to uncomment/use them in the future.
    ///
    /// access_token: String,
    /// token_type: String,
    /// expires_in: usize,
    /// id_token: String,
    refresh_token: String,
}

/// Response body from DeviceRefreshTokenRequest.
///
/// These fields are dictated by Google Identity.
#[derive(Debug, Deserialize, PartialEq)]
struct DeviceRefreshTokenError {
    /// What went wrong.
    error: String,
}

/// Performs steps to get a refresh token from scratch.
///
/// This may involve user interaction such as opening a browser window..
pub async fn new_refresh_token<I>(ui: &I) -> Result<String>
where
    I: structured_ui::Interface + Sync,
{
    let https_client = new_https_client();
    inner_new_refresh_token(&https_client, ui).await
}

async fn inner_new_refresh_token<I>(https_client: &HttpsClient, ui: &I) -> Result<String>
where
    I: structured_ui::Interface + Sync,
{
    tracing::debug!("device_new_refresh_token");
    let device_info = get_device_code(&https_client).await.context("getting device code")?;

    let mut notice = structured_ui::Notice::builder();
    let within = rough_human_duration(device_info.expires_in);
    notice.title("Device auth").message(format!(
        "Please open a browser to {}\nand enter the code {} when asked.\n\
        This may be done on a separate device, such as a smartphone and needs \
        to be done within {}.\nTo cancel, press ctrl+c.",
        device_info.verification_url, device_info.user_code, within
    ));
    ui.present(&structured_ui::Presentation::Notice(notice))?;

    let expiry = Duration::from_secs(device_info.expires_in);
    let start = Instant::now();
    while start.elapsed() < expiry {
        fuchsia_async::Timer::new(Duration::from_secs(device_info.interval)).await;
        if let Some(a) = poll_for_refresh_token(&device_info.device_code, https_client)
            .await
            .context("polling for refresh token")?
        {
            return Ok(a.refresh_token);
        }
    }
    bail!(
        "Too much time has passed since the code above was created. Please \
        try the command again (and more quickly approve the auth access)."
    );
}

/// Get url and code to ask the user to approve auth.
async fn get_device_code(https_client: &HttpsClient) -> Result<DeviceCodeResponse> {
    let req_body = DeviceCodeRequest { client_id: CLIENT_ID, scope: AUTH_SCOPE };
    let body = serde_json::to_vec(&req_body)?;
    let req = Request::builder()
        .method(Method::POST)
        .uri("https://oauth2.googleapis.com/device/code")
        .body(Body::from(body))?;

    let res = https_client.request(req).await?;
    tracing::debug!("{:?}", res);
    if !res.status().is_success() {
        let bytes = hyper::body::to_bytes(res.into_body()).await?;
        let error: DeviceCodeError = serde_json::from_slice(&bytes)?;
        tracing::debug!(
            "Error while getting device code: {:?}, {:?}, {:?}",
            error.error,
            error.error_description,
            error.error_uri
        );
        match error.error.as_ref() {
            "invalid_client" => {
                bail!("Known issue: Unable to get device auth until the Client ID is updated.")
            }
            _ => bail!(
                "Unable to get device auth. Please check your network connection and try again."
            ),
        }
    }
    let bytes = hyper::body::to_bytes(res.into_body()).await?;
    let info: DeviceCodeResponse = serde_json::from_slice(&bytes)?;
    Ok(info)
}

/// Poll the server to see if the user has granted permission.
async fn poll_for_refresh_token(
    device_code: &str,
    https_client: &HttpsClient,
) -> Result<Option<DeviceRefreshTokenResponse>> {
    let req_body = DeviceRefreshTokenRequest {
        client_id: CLIENT_ID,
        client_secret: CLIENT_SECRET,
        code: device_code,
        grant_type: "http://oauth.net/grant_type/device/1.0",
    };
    let body = serde_json::to_vec(&req_body)?;
    let req = Request::builder()
        .method(Method::POST)
        .uri("https://oauth2.googleapis.com/device/code")
        .body(Body::from(body))?;

    let res = https_client.request(req).await?;
    if !res.status().is_success() {
        let bytes = hyper::body::to_bytes(res.into_body()).await?;
        let body: DeviceRefreshTokenError = serde_json::from_slice(&bytes)?;
        tracing::debug!("Polling for device refresh token: {:?}", body);
        return Ok(None);
    }
    let bytes = hyper::body::to_bytes(res.into_body()).await?;
    let info: DeviceRefreshTokenResponse = serde_json::from_slice(&bytes)?;
    Ok(Some(info))
}

/// If the time is less than 2x of a common time length, report that duration in
/// a human readable form.
///
/// E.g. 130 seconds translates to "2+ minutes".
fn rough_human_duration(seconds: u64) -> String {
    match seconds {
        0..=119 => format!("{} seconds", seconds),
        x if x % 86400 == 0 => format!("{} days", seconds / 86400),
        x if x % 3600 == 0 => format!("{} hours", seconds / 3600),
        x if x % 60 == 0 => format!("{} minutes", seconds / 60),
        120..=7199 => format!("{}+ minutes", seconds / 60),
        7200..=172799 => format!("{}+ hours", seconds / 3600),
        _ => format!("{}+ days", seconds / 86400),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_inner_new_refresh_token() {
        let mut https_client = HttpsClient::mock();

        let req_body = DeviceCodeRequest { client_id: CLIENT_ID, scope: AUTH_SCOPE };
        let body = serde_json::to_vec(&req_body).expect("serde_json::to_vec");
        let req = Request::builder()
            .method(Method::POST)
            .uri("https://oauth2.googleapis.com/device/code")
            .body(Body::from(body))
            .expect("Request::builder");
        let builder = http::Response::builder().status(http::StatusCode::OK);
        let res = builder.body(Body::from(
            r#"{
                "device_code": "fake_device_code",
                "user_code": "fake_user_code",
                "verification_url": "https://example.com",
                "expires_in": 1,
                "interval": 1
            }"#,
        ));
        https_client.expect(req, res);

        let req_body = DeviceRefreshTokenRequest {
            client_id: CLIENT_ID,
            client_secret: CLIENT_SECRET,
            code: "fake_device_code",
            grant_type: "http://oauth.net/grant_type/device/1.0",
        };
        let body = serde_json::to_vec(&req_body).expect("serde_json::to_vec");
        let req = Request::builder()
            .method(Method::POST)
            .uri("https://oauth2.googleapis.com/device/code")
            .body(Body::from(body))
            .expect("Request::builder");
        let builder = http::Response::builder().status(http::StatusCode::OK);
        let res = builder.body(Body::from(
            r#"{
                "refresh_token": "fake_token"
            }"#,
        ));
        https_client.expect(req, res);

        let ui = structured_ui::MockUi::new();
        let refresh_token =
            inner_new_refresh_token(&https_client, &ui).await.expect("inner_new_refresh_token");
        assert_eq!(refresh_token, "fake_token");
    }

    #[test]
    fn test_rough_human_duration() {
        assert_eq!(rough_human_duration(0), "0 seconds");
        assert_eq!(rough_human_duration(60), "60 seconds");
        assert_eq!(rough_human_duration(119), "119 seconds");

        assert_eq!(rough_human_duration(60 * 2), "2 minutes");
        assert_eq!(rough_human_duration(60 * 2 + 1), "2+ minutes");
        assert_eq!(rough_human_duration(60 * 3), "3 minutes");
        assert_eq!(rough_human_duration(60 * 3 + 1), "3+ minutes");

        assert_eq!(rough_human_duration(3600 * 2), "2 hours");
        assert_eq!(rough_human_duration(3600 * 2 + 1), "2+ hours");
        assert_eq!(rough_human_duration(3600 * 3), "3 hours");
        assert_eq!(rough_human_duration(3600 * 3 + 1), "3+ hours");

        assert_eq!(rough_human_duration(86400 * 2), "2 days");
        assert_eq!(rough_human_duration(86400 * 2 + 1), "2+ days");
        assert_eq!(rough_human_duration(86400 * 3), "3 days");
        assert_eq!(rough_human_duration(86400 * 3 + 1), "3+ days");
    }
}
