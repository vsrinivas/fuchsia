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

#[derive(Default)]
// Because we don't yet have a system time we need a custom verifier
// that records the handshake information needed to perform a deferred
// trust evaluation
struct RecordingVerifier {
    presented_certs: Mutex<RefCell<Vec<Certificate>>>,
}

impl RecordingVerifier {
    // This is a standard TLS certificate verification, just using
    // the certificate chain we stored during the TLS handshake
    pub fn verify(
        &self,
        dns_name: webpki::DNSNameRef<'_>,
        time: webpki::Time,
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
            &webpki_roots_fuchsia::TLS_SERVER_ROOTS,
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

/// Makes a best effort to get network time via an HTTPS connection to
/// `hostname`.
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
pub async fn get_network_time(hostname: &str) -> Result<DateTime, HttpsDateError> {
    let dns_name = webpki::DNSNameRef::try_from_ascii_str(hostname)
        .map_err(|_| HttpsDateError::InvalidHostname)?;

    let url = format!("https://{}/", hostname);
    let url = url.parse::<hyper::Uri>().map_err(|_| HttpsDateError::InvalidHostname)?;
    let verifier = Arc::new(RecordingVerifier::default());

    // Because we don't currently have any idea what the "true" time is
    // we need to use a non-standard verifier, `RecordingVerifier`, to allow
    // us to defer trust evaluation until after we've parsed the response.
    let mut config = rustls::ClientConfig::new();
    config.root_store.add_server_trust_anchors(&webpki_roots_fuchsia::TLS_SERVER_ROOTS);
    config
        .dangerous()
        .set_certificate_verifier(Arc::clone(&verifier) as Arc<dyn rustls::ServerCertVerifier>);

    let client = fuchsia_hyper::new_https_client_dangerous(config, Default::default());

    let response = client.get(url).await.map_err(|_| HttpsDateError::NetworkError)?;

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

    // Per RFC7231 the date header is specified as RFC2822
    let response_time = chrono::DateTime::parse_from_rfc2822(&date_header)
        .map_err(|_| HttpsDateError::DateFormatError)?;

    // Finally verify the the certificate chain against the response time
    let webpki_time = webpki::Time::from_seconds_since_unix_epoch(response_time.timestamp() as u64);
    verifier.verify(dns_name, webpki_time)?;
    Ok(response_time)
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
    // These tests all interpret network errors as being passing results
    // in order to prevent flakiness due to unavoidable network flakiness.
    use super::*;
    use anyhow::Error;
    use fuchsia_async as fasync;

    #[ignore]
    #[fasync::run_singlethreaded(test)]
    async fn test_get_network_time() -> Result<(), Error> {
        let date = get_network_time("google.com").await;
        if date.is_err() {
            assert!(date.unwrap_err().is_network_error());
            return Ok(());
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_hostname() -> Result<(), Error> {
        let error = get_network_time("google com").await.unwrap_err();
        assert!(error.is_network_error() || error == HttpsDateError::InvalidHostname);
        Ok(())
    }
}
