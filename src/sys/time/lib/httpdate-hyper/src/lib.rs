// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_hyper;
use hyper;
use rustls::Certificate;
use std::cell::RefCell;
use std::sync::{Arc, Mutex};
use webpki;
use webpki_roots_fuchsia;

type DateTime = chrono::DateTime<chrono::FixedOffset>;

#[derive(Debug, PartialEq)]
pub enum HttpsDateError {
    InvalidHostname,
    SchemeNotHttps,
    NoCertificatesPresented,
    NetworkError,
    NoDateInResponse,
    InvalidCertificateChain,
    CorruptLeafCertificate,
    DateFormatError,
}

impl std::error::Error for HttpsDateError {}
impl std::fmt::Display for HttpsDateError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}

// I'd love to drop RSA here, but google.com doesn't yet serve ECDSA
static ALLOWED_SIG_ALGS: &[&webpki::SignatureAlgorithm] = &[
    &webpki::ECDSA_P256_SHA256,
    &webpki::ECDSA_P256_SHA384,
    &webpki::ECDSA_P384_SHA256,
    &webpki::ECDSA_P384_SHA384,
    &webpki::RSA_PKCS1_2048_8192_SHA256,
    &webpki::RSA_PKCS1_2048_8192_SHA384,
    &webpki::RSA_PKCS1_2048_8192_SHA512,
    &webpki::RSA_PKCS1_3072_8192_SHA384,
];

// Because we don't yet have a system time we need a custom verifier
// that records the handshake information needed to perform a deferred
// trust evaluation
#[derive(Default)]
struct RecordingVerifier {
    presented_certs: Mutex<RefCell<Vec<Certificate>>>,
}

impl RecordingVerifier {
    // Verify the certificate chain stored during the TLS handshake against the
    // given |time| and |trust_anchors| using standard TLS verification.
    pub fn verify(
        &self,
        dns_name: webpki::DNSNameRef<'_>,
        time: webpki::Time,
        trust_anchors: &webpki::TLSServerTrustAnchors<'static>,
    ) -> Result<(), HttpsDateError> {
        let presented_certs = self.presented_certs.lock().unwrap();
        let presented_certs = presented_certs.borrow();
        if presented_certs.len() == 0 {
            return Err(HttpsDateError::NoCertificatesPresented);
        };

        let untrusted_der: Vec<&[u8]> =
            presented_certs.iter().map(|certificate| certificate.0.as_slice()).collect();
        let leaf = webpki::EndEntityCert::from(untrusted_der[0])
            .map_err(|_| HttpsDateError::CorruptLeafCertificate)?;

        leaf.verify_is_valid_tls_server_cert(
            ALLOWED_SIG_ALGS,
            trust_anchors,
            &untrusted_der[1..],
            time,
        )
        .map_err(|_| HttpsDateError::InvalidCertificateChain)?;

        leaf.verify_is_valid_for_dns_name(dns_name)
            .map_err(|_| HttpsDateError::InvalidCertificateChain)
    }
}

impl rustls::ServerCertVerifier for RecordingVerifier {
    fn verify_server_cert(
        &self,
        _root_store: &rustls::RootCertStore,
        presented_certs: &[rustls::Certificate],
        _dns_name: webpki::DNSNameRef<'_>,
        _ocsp_response: &[u8],
    ) -> Result<rustls::ServerCertVerified, rustls::TLSError> {
        // Don't attempt to verify trust, just store the necessary details
        // for deferred evaluation
        *self.presented_certs.lock().unwrap().borrow_mut() = presented_certs.to_vec();
        Ok(rustls::ServerCertVerified::assertion())
    }
}

/// An HTTPS client that reports the contents of the response Date header.
pub struct NetworkTimeClient {
    /// The custom verifier used for certificate validation.
    verifier: Arc<RecordingVerifier>,
    /// The set of trust anchors used to verify a response.
    trust_anchors: &'static webpki::TLSServerTrustAnchors<'static>,
    /// The underlying client for making requests.
    client: fuchsia_hyper::HttpsClient,
}

impl NetworkTimeClient {
    /// Create a new `NetworkTimeClient` that uses the trust anchors provided through
    /// the 'root-ssl-certificates' component feature.
    pub fn new() -> Self {
        Self::new_with_trust_anchors(&webpki_roots_fuchsia::TLS_SERVER_ROOTS)
    }

    fn new_with_trust_anchors(
        trust_anchors: &'static webpki::TLSServerTrustAnchors<'static>,
    ) -> Self {
        // Because we don't currently have any idea what the "true" time is
        // we need to use a non-standard verifier, `RecordingVerifier`, to allow
        // us to defer trust evaluation until after we've parsed the response.
        let verifier = Arc::new(RecordingVerifier::default());
        let mut config = rustls::ClientConfig::new();
        config.root_store.add_server_trust_anchors(trust_anchors);
        config
            .dangerous()
            .set_certificate_verifier(Arc::clone(&verifier) as Arc<dyn rustls::ServerCertVerifier>);

        let client = fuchsia_hyper::new_https_client_dangerous(config, Default::default());

        NetworkTimeClient { verifier, client, trust_anchors }
    }

    /// Makes a best effort to get network time via an HTTPS connection to
    /// `uri`.
    ///
    /// # Errors
    ///
    /// `get_network_time` will return errors for network failures and TLS failures.
    ///
    /// # Panics
    ///
    /// `httpdate` needs access to the `root-ssl-certificates` sandbox feature. If
    /// it is not available this API will panic.
    ///
    /// # Security
    ///
    /// Validation of the TLS connection is deferred until after the handshake
    /// and then performed with respect to the time provided by the remote host.
    /// We validate the TLS connection against the system rootstore and time the server
    /// reports. This does mean that the best we can guarantee is that the host
    /// certificates were valid at some point, but the server can always provide a date
    /// that falls into the validity period of the certificates they provide.
    pub async fn get_network_time(&mut self, uri: hyper::Uri) -> Result<DateTime, HttpsDateError> {
        match uri.scheme_str() {
            Some("https") => (),
            _ => return Err(HttpsDateError::SchemeNotHttps),
        }
        let dns_name = match uri.host() {
            Some(host) => webpki::DNSNameRef::try_from_ascii_str(host)
                .map_err(|_| HttpsDateError::InvalidHostname)?,
            None => return Err(HttpsDateError::InvalidHostname),
        };

        let response =
            self.client.get(uri.clone()).await.map_err(|_| HttpsDateError::NetworkError)?;

        // Ok, so now we pull the Date header out of the response.
        // Technically the Date header is the date of page creation, but it's the best
        // we can do in the absence of a defined "accurate time" request.
        //
        // This has been suggested as being wrapped by an X-HTTPSTIME header,
        // or .well-known/time, but neither of these proposals appear to
        // have gone anywhere.
        let date_header: String = match response.headers().get("date") {
            Some(date) => date.to_str().map_err(|_| HttpsDateError::DateFormatError)?.to_string(),
            _ => return Err(HttpsDateError::NoDateInResponse),
        };

        // Per RFC7231 the date header is specified as RFC2822 with a UTC timezone.
        let response_time = DateTime::parse_from_rfc2822(&date_header)
            .map_err(|_| HttpsDateError::DateFormatError)?;
        if response_time.timezone().utc_minus_local() != 0 {
            return Err(HttpsDateError::DateFormatError);
        }

        // Finally verify the the certificate chain against the response time
        let webpki_time =
            webpki::Time::from_seconds_since_unix_epoch(response_time.timestamp() as u64);
        self.verifier.verify(dns_name, webpki_time, self.trust_anchors)?;
        Ok(response_time)
    }
}

#[cfg(test)]
impl HttpsDateError {
    pub fn is_network_error(&self) -> bool {
        match self {
            HttpsDateError::NetworkError => true,
            _ => false,
        }
    }
    pub fn is_pki_error(&self) -> bool {
        use HttpsDateError::*;
        match self {
            NoCertificatesPresented | InvalidCertificateChain | CorruptLeafCertificate => true,
            _ => false,
        }
    }
    pub fn is_date_error(&self) -> bool {
        use HttpsDateError::*;
        match self {
            DateFormatError => true,
            _ => false,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::Error;
    use fuchsia_async as fasync;
    use futures::{
        future::{ready, TryFutureExt},
        stream::TryStreamExt,
    };
    use hyper::{
        server::accept::from_stream,
        service::{make_service_fn, service_fn},
        Body, Response, Server, StatusCode,
    };
    use lazy_static::lazy_static;
    use log::warn;
    use std::{
        convert::Infallible,
        net::{Ipv6Addr, SocketAddr},
    };

    lazy_static! {
        static ref TEST_CERT_CHAIN: Vec<rustls::Certificate> =
            parse_pem(&include_str!("../certs/server.certchain"))
                .into_iter()
                .map(rustls::Certificate)
                .collect();
        static ref TEST_PRIVATE_KEY: rustls::PrivateKey =
            parse_pem(&include_str!("../certs/server.rsa")).pop().map(rustls::PrivateKey).unwrap();
        static ref CERT_NOT_BEFORE: DateTime =
            DateTime::parse_from_rfc3339(include_str!("../certs/notbefore").trim()).unwrap();
        static ref CERT_NOT_AFTER: DateTime =
            DateTime::parse_from_rfc3339(include_str!("../certs/notafter").trim()).unwrap();
        static ref TEST_CERT_ROOT: rustls::Certificate =
            parse_pem(&include_str!("../certs/ca.cert")).pop().map(rustls::Certificate).unwrap();
        static ref TEST_TRUST_ANCHORS: Vec<webpki::TrustAnchor<'static>> =
            vec![webpki::trust_anchor_util::cert_der_as_trust_anchor(TEST_CERT_ROOT.as_ref())
                .unwrap()];
        static ref TEST_TLS_SERVER_ROOTS: webpki::TLSServerTrustAnchors<'static> =
            webpki::TLSServerTrustAnchors(&TEST_TRUST_ANCHORS);
    }

    /// Spawn an HTTPS server that signs responses with TEST_PRIVATE_KEY and always returns
    /// `served_time` in the Date header. Listens for requests on 'localhost:port', where port
    /// is the returned port number.
    fn serve_fake(served_time: DateTime) -> u16 {
        let addr = SocketAddr::new(Ipv6Addr::LOCALHOST.into(), 0);
        let listener = fasync::net::TcpListener::bind(&addr).unwrap();
        let server_port = listener.local_addr().unwrap().port();

        let listener = listener
            .accept_stream()
            .map_err(Error::from)
            .map_ok(|(conn, _addr)| fuchsia_hyper::TcpStream { stream: conn });

        // build a server configuration using a test CA and cert chain
        let mut tls_config = rustls::ServerConfig::new(rustls::NoClientAuth::new());
        tls_config.set_single_cert(TEST_CERT_CHAIN.clone(), TEST_PRIVATE_KEY.clone()).unwrap();
        let tls_acceptor = tokio_rustls::TlsAcceptor::from(Arc::new(tls_config));

        // wrap incoming tcp streams
        let connections =
            listener.and_then(move |conn| tls_acceptor.accept(conn).map_err(Error::from));

        let served_time_arc = Arc::new(served_time);
        let make_svc = make_service_fn(move |_socket| {
            let time_arc = Arc::clone(&served_time_arc);
            ready(Ok::<_, Infallible>(service_fn(move |_req| {
                let time = Arc::clone(&time_arc);
                ready(
                    Response::builder()
                        .header("Date", time.to_rfc2822())
                        .status(StatusCode::OK)
                        .body(Body::from("")),
                )
            })))
        });
        let server = Server::builder(from_stream(connections))
            .executor(fuchsia_hyper::Executor)
            .serve(make_svc)
            .unwrap_or_else(|e| warn!("Error serving HTTPS server, {:?}", e));
        fasync::Task::spawn(server).detach();

        server_port
    }

    /// Simple pem parser that doesn't validate format.
    fn parse_pem(contents: &str) -> Vec<Vec<u8>> {
        // Blindly assume format is correct for our test
        let mut parsed = vec![];
        let mut current_encoded = vec![];
        for line in contents.split('\n') {
            if line.starts_with("-----BEGIN") {
                ()
            } else if line.starts_with("-----END") {
                let encoded = current_encoded.join("");
                current_encoded = vec![];
                parsed.push(base64::decode(&encoded).unwrap());
            } else {
                current_encoded.push(line.trim());
            }
        }
        parsed
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_network_time() {
        let set_time = *CERT_NOT_BEFORE + chrono::Duration::days(1);
        let open_port = serve_fake(set_time.clone());

        let mut client = NetworkTimeClient::new_with_trust_anchors(&TEST_TLS_SERVER_ROOTS);

        let url = format!("https://localhost:{}/", open_port).parse::<hyper::Uri>().unwrap();
        let date = client.get_network_time(url).await.unwrap();
        assert_eq!(date, set_time);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_untrusted_cert() {
        let time = *CERT_NOT_BEFORE + chrono::Duration::days(1);
        let open_port = serve_fake(time);

        // The test cert vended by our server should be rejected if we verify against real server
        // roots.
        let mut client =
            NetworkTimeClient::new_with_trust_anchors(&webpki_roots_fuchsia::TLS_SERVER_ROOTS);

        let url = format!("https://localhost:{}/", open_port).parse::<hyper::Uri>().unwrap();
        assert!(client.get_network_time(url).await.unwrap_err().is_pki_error());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_time_after_cert_expired() {
        let time = *CERT_NOT_AFTER + chrono::Duration::days(2);
        let open_port = serve_fake(time);

        let mut client = NetworkTimeClient::new_with_trust_anchors(&TEST_TLS_SERVER_ROOTS);

        let url = format!("https://localhost:{}/", open_port).parse::<hyper::Uri>().unwrap();
        assert!(client.get_network_time(url).await.unwrap_err().is_pki_error());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_http_rejected() {
        let mut client = NetworkTimeClient::new_with_trust_anchors(&TEST_TLS_SERVER_ROOTS);
        let url = "http://localhost/".parse::<hyper::Uri>().unwrap();
        assert_eq!(client.get_network_time(url).await.unwrap_err(), HttpsDateError::SchemeNotHttps);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_bad_timezone() {
        let set_time = (*CERT_NOT_BEFORE + chrono::Duration::days(1))
            .with_timezone(&chrono::FixedOffset::east(1 * 60 * 60));
        let open_port = serve_fake(set_time.clone());

        let mut client = NetworkTimeClient::new_with_trust_anchors(&TEST_TLS_SERVER_ROOTS);

        let url = format!("https://localhost:{}/", open_port).parse::<hyper::Uri>().unwrap();
        assert!(client.get_network_time(url).await.unwrap_err().is_date_error());
    }
}
