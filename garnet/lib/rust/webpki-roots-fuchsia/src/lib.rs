// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate lazy_static;

use webpki::trust_anchor_util::cert_der_as_trust_anchor;

static CERT_PATH: &'static str = "/config/ssl/cert.pem";

lazy_static! {
    // To meet the required lifetime constraints we need to chain together
    // a series of statics.
    static ref RAW_DATA: String = {
        // I could have an environment variable override this, but i'd want it keyed
        // on being a dev build so you couldn't harm trust in prod
        std::fs::read_to_string(CERT_PATH).expect("Unable to find root store")
    };
    static ref CERT_DERS: Vec<Vec<u8>> = {
        let lines: Vec<&str> = RAW_DATA
            .split('\n')
            .filter(|l| !l.is_empty() && !l.starts_with(':') && !l.starts_with('#'))
            .collect();
        let mut cert_ders = vec![];
        let mut i = 0;
        while i < lines.len() {
            let start = i;
            if lines[i] != "-----BEGIN CERTIFICATE-----" {
                panic!("Missing certificate prefix");
            }
            while i < lines.len() && lines[i] != "-----END CERTIFICATE-----" {
                i += 1;
            }
            if i == lines.len() {
                panic!("Missing certificate suffix");
            }
            let end = i;
            i += 1;
            let cert_base64 = &lines[start + 1..end].join("");

            let cert_bytes = base64::decode(cert_base64.as_bytes())
                .expect("Invalid base64 encoding in root store");
            cert_ders.push(cert_bytes);
        }
        cert_ders
    };
    static ref ROOTS: Vec<webpki::TrustAnchor<'static>> = {
        CERT_DERS.iter().map(|cert_bytes| {
            cert_der_as_trust_anchor(&cert_bytes)
                .expect("Parsing root certificate failed")
        }).collect()
    };

    pub static ref TLS_SERVER_ROOTS: webpki::TLSServerTrustAnchors<'static> =
        webpki::TLSServerTrustAnchors(&ROOTS);
}

#[cfg(test)]
mod test {
    #[test]
    fn test_load() {
        let webpki::TLSServerTrustAnchors(roots) =
            &crate::TLS_SERVER_ROOTS as &webpki::TLSServerTrustAnchors<'static>;
        assert_ne!(roots.len(), 0);
    }
}
