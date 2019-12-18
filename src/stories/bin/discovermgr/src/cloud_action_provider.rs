// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::models::Action, failure::Error, fidl_fuchsia_net_oldhttp as http,
    fuchsia_async as fasync, fuchsia_component as component, fuchsia_syslog::macros::*,
    fuchsia_zircon as zx, futures::io::AsyncReadExt, serde_derive::Deserialize,
};

/// Returns a vec of HttpHeaders used for Discover Cloud API request.
/// Returns error on failure.
///
/// Currently, discovermgr uses the device_name as Auth header.
/// http_headers need "Authorization: ApiKey xxxxxx"
/// for access authorization. Using the device_name as the apikey
/// is a hack and should be removed when we decide to use the
/// logged in user for access authorization.
///
async fn get_discover_cloud_http_headers(
    device_name: &str,
) -> Result<Vec<http::HttpHeader>, Error> {
    let mut http_headers = vec![http::HttpHeader {
        name: "Accept".to_string(),
        value: "application/json".to_string(),
    }];

    http_headers.push(http::HttpHeader {
        name: "Authorization".to_string(),
        value: format!("ApiKey {}", device_name).to_string(),
    });

    Ok(http_headers)
}

/// Fetch the contents of a URL as a string.
///
/// Returns error on failure.
///
/// Connects to the http service, sends a url request, and prints the response.
async fn http_get(url: &str, headers: Vec<http::HttpHeader>) -> Result<String, Error> {
    // Connect to the http service
    let net = component::client::connect_to_service::<http::HttpServiceMarker>()?;

    // Create a UrlLoader instance
    let (loader_proxy, server_end) = fidl::endpoints::create_proxy::<http::UrlLoaderMarker>()?;
    net.create_url_loader(server_end)?;

    // Send the UrlRequest to fetch the webpage
    let mut req = http::UrlRequest {
        url: url.to_string(),
        method: "GET".to_string(),
        headers: Some(headers),
        body: None,
        response_body_buffer_size: 0,
        auto_follow_redirects: true,
        cache_mode: http::CacheMode::Default,
        response_body_mode: http::ResponseBodyMode::Stream,
    };

    let resp = loader_proxy.start(&mut req).await?;
    if let Some(e) = resp.error {
        return Err(failure::err_msg(e.description.unwrap_or("".into())));
    }

    let mut socket = match resp.body.map(|x| *x) {
        Some(http::UrlBody::Stream(s)) => fasync::Socket::from_socket(s)?,
        _ => return Err(Error::from(zx::Status::BAD_STATE)),
    };

    // Copy the socket contents to a String.
    let mut output = vec![];
    socket.read_to_end(&mut output).await?;
    let result = String::from_utf8(output)?;
    fx_log_info!("Returned http bytes {} from {}", result.len(), url);
    Ok(result)
}

// helper to call serde and return the right type of error
fn serde_from_str(json: &str) -> Result<Vec<Action>, Error> {
    serde_json::from_str(json).map_err(|e| Error::from(e))
}

/// Fetch actions from cloud.
///
async fn get_actions_http(url: &str) -> Result<Vec<Action>, Error> {
    let name_provider =
        component::client::connect_to_service::<fidl_fuchsia_device::NameProviderMarker>()?;
    let device_name = name_provider.get_device_name().await?.map_err(zx::Status::from_raw)?;
    let http_headers = get_discover_cloud_http_headers(&device_name).await?;
    // Fetch the body and parse, returning error messages on failure
    http_get(url, http_headers)
        .await
        .or_else(|_| {
            fx_log_err!("Unable to fetch actions from cloud - ({})", url);
            Err(failure::err_msg(format!("Unable to fetch actions from cloud ({})", url)))
        })
        .and_then(|body| serde_from_str(body.as_str()))
        .or_else(|e: Error| {
            fx_log_err!("Unable to parse cloud actions - {:?}", e);
            Err(failure::err_msg(format!("Unable to parse cloud actions - ({:?})", e)))
        })
}

/// Fetch actions from cloud.
pub async fn get_cloud_actions() -> Result<Vec<Action>, Error> {
    // Configuration struct for this module contain the default cloud url
    let config: Config =
        serde_json::from_str(include_str!("../config/cloud_discover.json")).unwrap();
    get_actions_http(&config.url).await
}

/// The URL for cloud_discover is configurable
#[derive(Deserialize, Debug)]
struct Config {
    url: String,
}

#[cfg(test)]
mod test {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_http_get() -> Result<(), Error> {
        // Check for default url, expect an Err()
        assert!(get_cloud_actions().await.is_err());

        // Check for bad url, expect an Err()
        assert!(get_actions_http("http://example.com").await.is_err());

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_discover_cloud_http_headers() -> Result<(), Error> {
        let fake_device_name = "four-word-node-name";
        let response = get_discover_cloud_http_headers(&fake_device_name).await?;
        assert_eq!(
            response,
            vec![
                http::HttpHeader {
                    name: "Accept".to_string(),
                    value: "application/json".to_string(),
                },
                http::HttpHeader {
                    name: "Authorization".to_string(),
                    value: format!("ApiKey {}", fake_device_name).to_string(),
                },
            ]
        );

        Ok(())
    }
}
