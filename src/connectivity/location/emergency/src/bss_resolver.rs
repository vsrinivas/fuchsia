// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::bss_cache::{Bss, BssId},
    async_trait::async_trait,
    fidl::{Error as FidlError, Socket as FidlSocket},
    fidl_fuchsia_location_position::{Position, PositionExtras},
    fidl_fuchsia_mem::Buffer as MemBuffer,
    fidl_fuchsia_net_http::{
        Body as HttpBody, LoaderProxyInterface, Request as HttpRequest, Response as HttpResponse,
    },
    fuchsia_async::futures::io::AsyncReadExt,
    fuchsia_async::futures::Future,
    fuchsia_zircon as zx,
    itertools::Itertools,
    serde_json::{json, map::Map as JsonMap, value::Value as JsonValue},
    static_assertions::assert_eq_size_val,
    std::{borrow::Borrow, convert::TryFrom},
};

// Geographic constants.
const LATITUDE_RANGE: std::ops::RangeInclusive<f64> = -90.0..=90.0;
const LONGITUDE_RANGE: std::ops::RangeInclusive<f64> = -180.0..=180.0;

// Google Maps constants.
const SERVICE_URL: &'static str = "https://www.googleapis.com/geolocation/v1/geolocate";
const MAC_ADDR_KEY: &'static str = "macAddress";
const AP_LIST_KEY: &'static str = "wifiAccessPoints";
const RSSI_KEY: &'static str = "signalStrength";
const LOCATION_KEY: &'static str = "location";
const LATITUDE_KEY: &'static str = "lat";
const LONGITUDE_KEY: &'static str = "lng";
const ACCURACY_KEY: &'static str = "accuracy";

// HTTP constants.
const HTTP_METHOD_POST: &'static str = "POST";

#[async_trait(?Send)]
pub trait BssResolver {
    /// Resolves WLAN BSS (base-station) meta-data to a `Position`.
    async fn resolve<'a, I, T, U>(&self, bsses: I) -> Result<Position, ResolverError>
    where
        I: IntoIterator,
        I::Item: Borrow<(T, U)>,
        T: Borrow<BssId>,
        U: Borrow<Bss>;
}

/// A service for resolving WLAN BSS (base-station) meta-data to `Position`s.
pub struct RealBssResolver<L: LoaderProxyInterface> {
    http_loader: L,
    api_key: String,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ResolverError {
    NoBsses,
    Internal,
    Lookup,
}

impl<L: LoaderProxyInterface> RealBssResolver<L> {
    pub fn new(http_loader: L, api_key: impl Into<String>) -> Self {
        Self { http_loader, api_key: api_key.into() }
    }
}

#[async_trait(?Send)]
impl<L: LoaderProxyInterface> BssResolver for RealBssResolver<L> {
    async fn resolve<'a, I, T, U>(&self, bsses: I) -> Result<Position, ResolverError>
    where
        I: IntoIterator,
        I::Item: Borrow<(T, U)>,
        T: Borrow<BssId>,
        U: Borrow<Bss>,
    {
        let mut bsses = bsses.into_iter().peekable();
        if bsses.peek().is_none() {
            return Err(ResolverError::NoBsses);
        }
        parse_response(send_query(bsses, &self.api_key, &self.http_loader)?.await).await
    }
}

fn send_query<'a, I, T, U>(
    bsses: I,
    api_key: impl AsRef<str>,
    http_loader: &impl LoaderProxyInterface,
) -> Result<impl Future<Output = Result<HttpResponse, FidlError>>, ResolverError>
where
    I: Iterator,
    I::Item: Borrow<(T, U)>,
    T: Borrow<BssId>,
    U: Borrow<Bss>,
{
    let api_key = api_key.as_ref();
    let request_body = serialize_bsses(bsses);
    let request_size = {
        let body_size = request_body.len();
        assert_eq_size_val!(body_size, 0u64);
        u64::try_from(body_size).expect("failed to convert usize to u64")
    };
    let request_vmo = zx::Vmo::create(request_size).map_err(|_| ResolverError::Internal)?;
    let _ = request_vmo.write(&request_body.as_bytes(), 0).map_err(|_| ResolverError::Internal)?;
    Ok(http_loader.fetch(HttpRequest {
        method: Some(HTTP_METHOD_POST.to_owned()),
        url: Some(format!("{}?key={}", SERVICE_URL, api_key)),
        headers: None,
        body: Some(HttpBody::Buffer(MemBuffer { vmo: request_vmo, size: request_size })),
        deadline: None,
    }))
}

async fn parse_response(
    response: Result<HttpResponse, FidlError>,
) -> Result<Position, ResolverError> {
    let response: HttpResponse = response.map_err(|_fidl_error| ResolverError::Internal)?;
    let response: FidlSocket = response.body.ok_or(ResolverError::Lookup)?;
    let response: String = read_socket(response).await.ok_or(ResolverError::Lookup)?;
    json_to_position(serde_json::from_str(&response).map_err(|_| ResolverError::Lookup)?)
}

fn serialize_bsses<'a, I, T, U>(bsses: I) -> String
where
    I: Iterator,
    I::Item: Borrow<(T, U)>,
    T: Borrow<BssId>,
    U: Borrow<Bss>,
{
    let bsses = bsses
        .map(|item| bss_to_json(item.borrow().0.borrow(), item.borrow().1.borrow()))
        .collect::<Vec<_>>();
    json!({ AP_LIST_KEY: bsses }).to_string()
}

fn bss_to_json(bss_id: impl Borrow<BssId>, bss: impl Borrow<Bss>) -> JsonValue {
    let mut json = JsonMap::new();
    json.insert(
        MAC_ADDR_KEY.to_owned(),
        json!(bss_id.borrow().iter().map(|bss_byte| format!("{:02x}", bss_byte)).join(":")),
    );
    if let Some(rssi) = bss.borrow().rssi {
        json.insert(RSSI_KEY.to_owned(), json!(rssi));
    };
    JsonValue::from(json)
}

async fn read_socket(socket: zx::Socket) -> Option<String> {
    let mut buf = Vec::new();
    match fuchsia_async::Socket::from_socket(socket).ok() {
        Some(mut socket) => match socket.read_to_end(&mut buf).await {
            Ok(_num_bytes_read) => String::from_utf8(buf).ok(),
            Err(_) => None,
        },
        None => None,
    }
}

fn json_to_position(response: JsonValue) -> Result<Position, ResolverError> {
    let latitude = response[LOCATION_KEY][LATITUDE_KEY].as_f64().ok_or(ResolverError::Lookup)?;
    if !LATITUDE_RANGE.contains(&latitude) {
        return Err(ResolverError::Lookup);
    }

    let longitude = response[LOCATION_KEY][LONGITUDE_KEY].as_f64().ok_or(ResolverError::Lookup)?;
    if !LONGITUDE_RANGE.contains(&longitude) {
        return Err(ResolverError::Lookup);
    }

    let extras = match response[ACCURACY_KEY].as_f64() {
        Some(accuracy) => PositionExtras { accuracy_meters: Some(accuracy), altitude_meters: None },
        None => PositionExtras { accuracy_meters: None, altitude_meters: None },
    };

    Ok(Position { latitude, longitude, extras })
}

#[cfg(test)]
mod tests {
    mod request_generation {
        use {
            super::super::{test_doubles::HttpRequestValidator, *},
            fuchsia_async as fasync,
        };

        #[fasync::run_until_stalled(test)]
        async fn request_uses_post_method() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: Some(-30), frequency: Some(2412) })];
            let mut request_method = None;
            let http_loader = HttpRequestValidator::new(|request| {
                request_method = Some(request.method.expect("request had no method"));
            });
            let _ = RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await;
            assert_eq!(request_method.expect("request was never issued"), "POST".to_owned());
        }

        #[fasync::run_until_stalled(test)]
        async fn request_url_is_correct() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: Some(-30), frequency: Some(2412) })];
            let mut request_url = None;
            let http_loader = HttpRequestValidator::new(|request| {
                request_url = Some(request.url.expect("request had no url"));
            });
            let _ = RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await;
            assert_eq!(
                request_url.expect("request was never issued"),
                "https://www.googleapis.com/geolocation/v1/geolocate?key=fake_key".to_owned()
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn request_body_contains_all_bsses() {
            let bsses = vec![
                ([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None }),
                ([1, 1, 1, 1, 1, 1], Bss { rssi: None, frequency: None }),
            ];
            let mut request_body = None;
            let http_loader = HttpRequestValidator::new(|request| {
                request_body = Some(request.body.expect("request had no body"));
            });
            let _ = RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await;
            assert_eq!(
                strip_whitespace(read_request_body(
                    request_body.expect("request was never issued")
                )),
                strip_whitespace(
                    r#"{
                    "wifiAccessPoints":[
                        {"macAddress":"00:00:00:00:00:00"},
                        {"macAddress":"01:01:01:01:01:01"}
                    ]
                }"#
                )
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn request_body_includes_rssi() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: Some(-30), frequency: None })];
            let mut request_body = None;
            let http_loader = HttpRequestValidator::new(|request| {
                request_body = Some(request.body.expect("request had no body"));
            });
            let _ = RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await;
            assert_eq!(
                strip_whitespace(read_request_body(
                    request_body.expect("request was never issued")
                )),
                strip_whitespace(
                    r#"{
                    "wifiAccessPoints":[
                        {"macAddress":"00:00:00:00:00:00",
                         "signalStrength": -30}
                    ]
                }"#
                )
            )
        }

        #[fasync::run_until_stalled(test)]
        async fn skips_api_query_when_bsses_is_empty() {
            let http_loader =
                HttpRequestValidator::new(|_request| panic!("should not issue API query"));
            let _ = RealBssResolver::new(http_loader, "fake_key")
                .resolve(std::iter::empty::<(BssId, Bss)>())
                .await;
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_no_bsses_error_when_bsses_is_empty() {
            let http_loader = HttpRequestValidator::new(|_| ());
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key")
                    .resolve(std::iter::empty::<(BssId, Bss)>())
                    .await,
                Err(ResolverError::NoBsses)
            );
        }

        fn read_request_body(body: HttpBody) -> String {
            match body {
                HttpBody::Buffer(shared_buf) => {
                    let mut local_buf = vec![
                        0_u8;
                        usize::try_from(shared_buf.size).expect(
                            "internal error: failed to convert u64 to usize"
                        )
                    ];
                    shared_buf
                        .vmo
                        .read(&mut local_buf, 0)
                        .expect("internal error: failed to read from VMO");
                    String::from_utf8(local_buf)
                        .expect("internal error: failed to convert buffer to UTF-8")
                }
                HttpBody::Stream(_) => panic!("internal error: stream bodies not supported"),
            }
        }

        fn strip_whitespace<S: AsRef<str>>(input: S) -> String {
            input.as_ref().replace("\n", "").replace(" ", "")
        }
    }

    mod response_error_handling {
        use {
            super::super::{
                test_doubles::{HttpByteResponder, HttpFidlResponder},
                *,
            },
            fuchsia_async as fasync,
        };

        #[fasync::run_until_stalled(test)]
        async fn returns_internal_error_on_fidl_error() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpFidlResponder::new(|| Err(FidlError::InvalidHeader));
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Internal)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_response_has_no_body() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpFidlResponder::new(|| {
                Ok(HttpResponse {
                    error: None,
                    body: None,
                    final_url: None,
                    status_code: None,
                    status_line: None,
                    headers: None,
                    redirect: None,
                })
            });
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_body_is_unreadable() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpFidlResponder::new(|| {
                Ok(HttpResponse {
                    error: None,
                    body: Some(zx::Socket::from(zx::Handle::invalid())),
                    final_url: None,
                    status_code: None,
                    status_line: None,
                    headers: None,
                    redirect: None,
                })
            });
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_body_is_not_valid_json() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(b"hello world".to_vec());
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_body_is_not_a_dictionary() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(b"42".to_vec());
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_body_is_missing_location() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(b"{}".to_vec());
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_body_is_missing_latitude() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                          "lng": -0.1
                        },
                        "accuracy": 1200.4
                }"#
                .to_vec(),
            );
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_body_is_missing_longitude() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": 51.0,
                        },
                        "accuracy": 1200.4
                }"#
                .to_vec(),
            );
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_latitude_is_too_high() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": 90.1,
                            "lng": 0.0
                        }
                }"#
                .to_vec(),
            );
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_latitude_is_too_low() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": -90.1,
                            "lng": 0.0
                        }
                }"#
                .to_vec(),
            );
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_longitude_is_too_high() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": 0.0,
                            "lng": 180.1
                        }
                }"#
                .to_vec(),
            );
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_lookup_error_when_longitude_is_too_low() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": 0.0,
                            "lng": -180.1
                        }
                }"#
                .to_vec(),
            );
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Err(ResolverError::Lookup)
            );
        }
    }

    mod response_success_reporting {
        use {
            super::super::{test_doubles::HttpByteResponder, *},
            fuchsia_async as fasync,
            matches::assert_matches,
        };

        #[fasync::run_until_stalled(test)]
        async fn returns_success_when_all_fields_are_present() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": 51.0,
                            "lng": -0.1
                        },
                        "accuracy": 1200.4
                }"#
                .to_vec(),
            );
            assert_matches!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Ok(_)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_success_when_all_fields_except_accuracy_are_present() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": 51.0,
                            "lng": -0.1
                       }
                }"#
                .to_vec(),
            );
            assert_matches!(
                RealBssResolver::new(http_loader, "fake_key").resolve(bsses).await,
                Ok(_)
            );
        }
    }

    mod response_success_contents {
        use {
            super::super::{test_doubles::HttpByteResponder, *},
            fuchsia_async as fasync,
            matches::assert_matches,
        };

        #[fasync::run_until_stalled(test)]
        async fn provides_precise_latitude() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": 89.0,
                            "lng": 0
                       }
                }"#
                .to_vec(),
            );
            assert_matches!(
                RealBssResolver::new(http_loader, "fake_key")
                    .resolve(bsses)
                    .await.expect("position is none").latitude,
                // One degree of latitude is approximately 111 kilometers. By limiting rounding
                // error to 1/10,000,000 of a degree, we limit rounding error to
                // 111/10000 = 0.011 meters, or  1.1 cm.
                latitude if (88.999_999_9..89.000_000_1).contains(&latitude)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn provides_precise_longitude() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": 0,
                            "lng": 179.0
                       }
                }"#
                .to_vec(),
            );
            assert_matches!(
                RealBssResolver::new(http_loader, "fake_key")
                    .resolve(bsses)
                    .await.expect("no position").longitude,
                // At the equator, one degree of longitude is approximately 111 kilometers.
                // By limiting rounding error to 1/10,000,000 of a degree, we limit rounding error
                // to 111/10000 = 0.011 meters, or  1.1 cm.
                longitude if (178.999_999_9..179.099_999_9).contains(&longitude)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn provides_precise_accuracy_when_present() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": 51.0,
                            "lng": -0.1
                        },
                        "accuracy": 1200.4
                }"#
                .to_vec(),
            );
            assert_matches!(
                RealBssResolver::new(http_loader, "fake_key")
                    .resolve(bsses)
                    .await.expect("no position").extras.accuracy_meters.expect("accuracy is none"),
                accuracy if (1200.39..1200.41).contains(&accuracy)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn does_not_fabricate_accuracy() {
            let bsses = vec![([0, 0, 0, 0, 0, 0], Bss { rssi: None, frequency: None })];
            let http_loader = HttpByteResponder::new(
                br#"{
                        "location": {
                            "lat": 51.0,
                            "lng": -0.1
                        }
                }"#
                .to_vec(),
            );
            assert_eq!(
                RealBssResolver::new(http_loader, "fake_key")
                    .resolve(bsses)
                    .await
                    .expect("no position")
                    .extras
                    .accuracy_meters,
                None
            );
        }
    }
}

#[cfg(test)]
mod test_doubles {
    use {
        super::*,
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_net_http::LoaderClientMarker,
        fuchsia_async::futures::future::{ready, Ready},
        std::sync::RwLock,
    };

    const HTTP_OK: u32 = 200;

    type FetchResponse = Result<HttpResponse, FidlError>;

    // Test double that
    // 1) invokes `validate` to run test assertions, and
    // 2) returns HTTP_OK, assuming `validate` did not abort
    pub(super) struct HttpRequestValidator<F>
    where
        F: FnMut(HttpRequest) + Send + Sync,
    {
        // RwLock is needed because
        // a) we need interior mutability for F to be FnMut, and
        // b) we need to implement Send and Sync.
        validate: RwLock<F>,
    }

    // Test double that invokes a function which yields `FetchResponse`,
    // and returns that as a FIDL response. Useful for testing error
    // handling.
    pub(super) struct HttpFidlResponder<F>
    where
        F: Fn() -> FetchResponse + Send + Sync,
    {
        fetch: F,
    }

    // Test double that invokes a function which yields a `Vec<u8>`,
    // and returns the Vec as the HTTP response. Useful for testing
    // response parsing.
    pub(super) struct HttpByteResponder {
        response: Vec<u8>,
    }

    impl<F> HttpRequestValidator<F>
    where
        F: FnMut(HttpRequest) + Send + Sync,
    {
        pub(super) fn new(validate: F) -> Self {
            Self { validate: RwLock::new(validate) }
        }
    }

    impl<F> LoaderProxyInterface for HttpRequestValidator<F>
    where
        F: FnMut(HttpRequest) + Send + Sync,
    {
        type FetchResponseFut = Ready<Result<HttpResponse, FidlError>>;

        fn fetch(&self, request: HttpRequest) -> Self::FetchResponseFut {
            // Note: the `&mut *` here is due to https://github.com/rust-lang/rust/issues/65489
            let validate = &mut *self.validate.write().expect("internal error");
            let final_url = request.url.clone();
            validate(request);
            ready(Ok(HttpResponse {
                error: None,
                body: None,
                final_url,
                status_code: Some(HTTP_OK),
                status_line: None,
                headers: None,
                redirect: None,
            }))
        }

        fn start(
            &self,
            _request: HttpRequest,
            _client: ClientEnd<LoaderClientMarker>,
        ) -> Result<(), FidlError> {
            panic!("internal error: this fake does not implement `start()`");
        }
    }

    impl<F> HttpFidlResponder<F>
    where
        F: Fn() -> FetchResponse + Send + Sync,
    {
        pub(super) fn new(fetch: F) -> Self {
            Self { fetch }
        }
    }

    impl<F> LoaderProxyInterface for HttpFidlResponder<F>
    where
        F: Fn() -> FetchResponse + Send + Sync,
    {
        type FetchResponseFut = Ready<FetchResponse>;

        fn fetch(&self, _request: HttpRequest) -> Self::FetchResponseFut {
            ready((self.fetch)())
        }

        fn start(
            &self,
            _request: HttpRequest,
            _client: ClientEnd<LoaderClientMarker>,
        ) -> Result<(), FidlError> {
            panic!("internal error: this stub does not implement `start()`");
        }
    }

    impl HttpByteResponder {
        pub(super) fn new(response: Vec<u8>) -> Self {
            Self { response }
        }
    }

    impl LoaderProxyInterface for HttpByteResponder {
        type FetchResponseFut = Ready<FetchResponse>;

        fn fetch(&self, _request: HttpRequest) -> Self::FetchResponseFut {
            let (local_socket, remote_socket) =
                zx::Socket::create(zx::SocketOpts::STREAM).expect("internal error");
            local_socket.write(&self.response).expect("internal error");
            ready(Ok(HttpResponse {
                error: None,
                body: Some(remote_socket),
                final_url: None,
                status_code: Some(HTTP_OK),
                status_line: None,
                headers: None,
                redirect: None,
            }))
        }

        fn start(
            &self,
            _request: HttpRequest,
            _client: ClientEnd<LoaderClientMarker>,
        ) -> Result<(), FidlError> {
            panic!("internal error: this stub does not implement `start()`");
        }
    }
}
