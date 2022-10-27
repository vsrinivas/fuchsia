// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use http::{Response, Uri};
use http_uri_ext::HttpUriExt as _;
use hyper::header::ETAG;
use p256::ecdsa::{signature::Verifier as _, DerSignature};
use rand::{thread_rng, Rng};
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use sha2::{digest, Digest, Sha256};
use signature::Signature;
use std::{collections::HashMap, convert::TryInto, fmt, fmt::Debug};

/// Error enum listing different kinds of CUPv2 decoration errors.
#[derive(Debug, thiserror::Error)]
pub enum CupDecorationError {
    #[error("could not serialize request.")]
    SerializationError(#[from] serde_json::Error),
    #[error("could not parse existing URI.")]
    ParseError(#[from] http::uri::InvalidUri),
    #[error("could not append query parameter.")]
    AppendQueryParameterError(#[from] http_uri_ext::Error),
}

/// Error enum listing different kinds of CUPv2 verification errors.
#[derive(Debug, thiserror::Error)]
pub enum CupVerificationError {
    #[error("etag header missing.")]
    EtagHeaderMissing,
    #[error("etag header is not a string.")]
    EtagNotString(hyper::header::ToStrError),
    #[error("etag header is malformed.")]
    EtagMalformed,
    #[error("etag header's request hash is malformed.")]
    RequestHashMalformed,
    #[error("etag header's request hash doesn't match.")]
    RequestHashMismatch,
    #[error("etag header's signature is malformed.")]
    SignatureMalformed,
    #[error("specified public key ID not found in internal map.")]
    SpecifiedPublicKeyIdMissing,
    #[error("could not verify etag header's signature.")]
    SignatureError(#[from] ecdsa::Error),
}

/// By convention, this is always the u64 hash of the public key
/// value.
pub type PublicKeyId = u64;
pub type PublicKey = p256::ecdsa::VerifyingKey;

fn from_pem<'de, D>(deserializer: D) -> Result<PublicKey, D::Error>
where
    D: Deserializer<'de>,
{
    use serde::de;
    let s = String::deserialize(deserializer)?;
    s.parse().map_err(de::Error::custom)
}

fn to_pem<S>(public_key: &PublicKey, serializer: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    use pkcs8::EncodePublicKey;
    use serde::ser;
    serializer.serialize_str(
        &elliptic_curve::PublicKey::from(public_key)
            .to_public_key_pem(pkcs8::LineEnding::LF)
            .map_err(ser::Error::custom)?,
    )
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct PublicKeyAndId {
    #[serde(deserialize_with = "from_pem", serialize_with = "to_pem")]
    pub key: PublicKey,
    pub id: PublicKeyId,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct PublicKeys {
    /// The latest public key will be used when decorating requests.
    pub latest: PublicKeyAndId,
    /// Historical public keys and IDs. May be empty.
    pub historical: Vec<PublicKeyAndId>,
}

#[derive(PartialEq, Eq, Debug, Copy, Clone)]
pub struct Nonce([u8; 32]);

impl From<[u8; 32]> for Nonce {
    fn from(array: [u8; 32]) -> Self {
        Nonce(array)
    }
}
impl From<&[u8; 32]> for Nonce {
    fn from(array: &[u8; 32]) -> Self {
        Nonce(*array)
    }
}

#[allow(clippy::from_over_into)]
impl Into<[u8; 32]> for Nonce {
    fn into(self) -> [u8; 32] {
        self.0
    }
}

impl Default for Nonce {
    fn default() -> Self {
        Self::new()
    }
}

impl Nonce {
    pub fn new() -> Nonce {
        let mut nonce_bits = [0_u8; 32];
        thread_rng().fill(&mut nonce_bits[..]);
        Nonce(nonce_bits)
    }
}

impl fmt::Display for Nonce {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", hex::encode(self.0))
    }
}

/// Request decoration return type, containing request internals. Clients of this
/// library can call .hash() and store/retrieve the hash, or they can inspect the
/// request, public key ID, nonce used if necessary.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RequestMetadata {
    pub request_body: Vec<u8>,
    pub public_key_id: PublicKeyId,
    pub nonce: Nonce,
}

/// An interface to an under-construction server request, providing read/write
/// access to the URI and read access to the serialized request body.
pub trait CupRequest {
    /// Get the request URI.
    fn get_uri(&self) -> &str;
    /// Set a new request URI.
    fn set_uri(&mut self, uri: String);
    /// Get a serialized copy of the request body.
    fn get_serialized_body(&self) -> serde_json::Result<Vec<u8>>;
}

// General trait for a decorator which knows how to decorate and verify CUPv2
// requests.
pub trait Cupv2RequestHandler {
    /// Decorate an outgoing client request with query parameters `cup2key`.
    /// Returns a struct of request metadata, the hash of which can be stored and
    /// used later.
    fn decorate_request(
        &self,
        request: &mut impl CupRequest,
    ) -> Result<RequestMetadata, CupDecorationError>;

    /// Examines an incoming client request with an ETag HTTP Header. Returns an
    /// error if the response is not authentic.
    fn verify_response(
        &self,
        request_metadata: &RequestMetadata,
        resp: &Response<Vec<u8>>,
        public_key_id: PublicKeyId,
    ) -> Result<DerSignature, CupVerificationError>;
}

// General trait for something which can verify CUPv2 signatures.
pub trait Cupv2Verifier {
    /// The same behavior as verify_response, but designed for verifying stored
    /// signatures which are not hyper-aware.
    fn verify_response_with_signature(
        &self,
        ecdsa_signature: &DerSignature,
        request_body: &[u8],
        response_body: &[u8],
        public_key_id: PublicKeyId,
        nonce: &Nonce,
    ) -> Result<(), CupVerificationError>;
}

pub trait Cupv2Handler: Cupv2RequestHandler + Cupv2Verifier {}

impl<T> Cupv2Handler for T where T: Cupv2RequestHandler + Cupv2Verifier {}

// Default Cupv2Handler.
#[derive(Debug)]
pub struct StandardCupv2Handler {
    /// Device-wide map from public key ID# to public key. Should be ingested at
    /// startup. This map should never be empty.
    parameters_by_id: HashMap<PublicKeyId, PublicKey>,
    latest_public_key_id: PublicKeyId,
}

impl StandardCupv2Handler {
    /// Constructor for the standard CUPv2 handler.
    pub fn new(public_keys: &PublicKeys) -> Self {
        Self {
            parameters_by_id: std::iter::once(&public_keys.latest)
                .chain(&public_keys.historical)
                .map(|k| (k.id, k.key))
                .collect(),
            latest_public_key_id: public_keys.latest.id,
        }
    }
}

impl Cupv2RequestHandler for StandardCupv2Handler {
    fn decorate_request(
        &self,
        request: &mut impl CupRequest,
    ) -> Result<RequestMetadata, CupDecorationError> {
        // Per
        // https://github.com/google/omaha/blob/master/doc/ClientUpdateProtocolEcdsa.md#top-level-description,
        //
        // formatting will be similar to CUP -- namely: “cup2key=%d:%u” where
        // the first parameter is the key pair id, and the second is the client
        // freshness nonce.

        let public_key_id: PublicKeyId = self.latest_public_key_id;

        let nonce = Nonce::new();

        let uri: Uri = request.get_uri().parse()?;
        let uri = uri.append_query_parameter("cup2key", &format!("{}:{}", public_key_id, nonce))?;
        request.set_uri(uri.to_string());

        Ok(RequestMetadata { request_body: request.get_serialized_body()?, public_key_id, nonce })
    }

    fn verify_response(
        &self,
        request_metadata: &RequestMetadata,
        resp: &Response<Vec<u8>>,
        public_key_id: PublicKeyId,
    ) -> Result<DerSignature, CupVerificationError> {
        // Per
        // https://github.com/google/omaha/blob/master/doc/ClientUpdateProtocolEcdsa.md#top-level-description,
        //
        // The client receives the response XML, observed client hash, and ECDSA
        // signature. It concatenates its copy of the request hash to the
        // response XML, and attempts to verify the ECDSA signature using its
        // public key. If the signature does not match, the client recognizes
        // that the server response has been tampered in transit, and rejects
        // the exchange.
        //
        // The client then compares the SHA-256 hash in the response to the
        // original hash of the request. If the hashes do not match, the client
        // recognizes that the request has been tampered in transit, and rejects
        // the exchange.

        let etag_header = resp
            .headers()
            .get(ETAG)
            .ok_or(CupVerificationError::EtagHeaderMissing)?
            .to_str()
            .map_err(CupVerificationError::EtagNotString)
            .map(parse_etag)?;

        let (encoded_signature, hex_hash): (&str, &str) =
            etag_header.split_once(':').ok_or(CupVerificationError::EtagMalformed)?;

        let actual_hash =
            &hex::decode(hex_hash).map_err(|_| CupVerificationError::RequestHashMalformed)?;

        let request_body_hash = Sha256::digest(&request_metadata.request_body);
        if *request_body_hash != *actual_hash {
            return Err(CupVerificationError::RequestHashMismatch);
        }

        let signature = DerSignature::from_bytes(
            &hex::decode(encoded_signature)
                .map_err(|_| CupVerificationError::SignatureMalformed)?,
        )?;

        let () = self.verify_response_with_signature(
            &signature,
            &request_metadata.request_body,
            resp.body(),
            public_key_id,
            &request_metadata.nonce,
        )?;

        Ok(signature)
    }
}

pub fn make_transaction_hash(
    request_body: &[u8],
    response_body: &[u8],
    public_key_id: PublicKeyId,
    nonce: &Nonce,
) -> digest::Output<Sha256> {
    let request_hash = Sha256::digest(request_body);
    let response_hash = Sha256::digest(response_body);
    let cup2_urlparam = format!("{}:{}", public_key_id, nonce);

    let mut hasher = Sha256::new();
    hasher.update(request_hash);
    hasher.update(response_hash);
    hasher.update(&cup2_urlparam);
    hasher.finalize()
}

impl Cupv2Verifier for StandardCupv2Handler {
    fn verify_response_with_signature(
        &self,
        ecdsa_signature: &DerSignature,
        request_body: &[u8],
        response_body: &[u8],
        public_key_id: PublicKeyId,
        nonce: &Nonce,
    ) -> Result<(), CupVerificationError> {
        let transaction_hash =
            make_transaction_hash(request_body, response_body, public_key_id, nonce);

        let public_key: &PublicKey = self
            .parameters_by_id
            .get(&public_key_id)
            .ok_or(CupVerificationError::SpecifiedPublicKeyIdMissing)?;
        // Since we pass DerSignature by reference, and it doesn't implement
        // clone, we must reconstitute it into bytes and then back into
        // der::Signature,
        let der_signature: ecdsa::der::Signature<p256::NistP256> =
            ecdsa_signature.as_ref().try_into()?;
        // and then into Signature for verification.
        let signature: ecdsa::Signature<p256::NistP256> = der_signature.try_into()?;
        Ok(public_key.verify(&transaction_hash, &signature)?)
    }
}

fn parse_etag(etag: &str) -> &str {
    // ETag headers are wrapped in double quotes, and can optionally have a W/
    // prefix. Examples:
    //     ETag: "33a64df551425fcc55e4d42a148795d9f25f89d4"
    //     ETag: W/"0815"
    match etag.as_bytes() {
        // If W/".." is present, strip this prefix and the trailing quote.
        //
        // NB: Since the &str is valid utf8, removing bytes results in a valid
        // byte sequence which can be reconstituted into a utf8 without
        // checking.
        [b'W', b'/', b'\"', inner @ .., b'\"'] => unsafe { std::str::from_utf8_unchecked(inner) },
        // If only ".." is present, strip the surrounding quotes.
        [b'\"', inner @ .., b'\"'] => unsafe { std::str::from_utf8_unchecked(inner) },
        // Otherwise, leave the str unchanged.
        _ => etag,
    }
}

pub mod test_support {
    use super::*;
    use crate::{
        protocol::request::{Request, RequestWrapper},
        request_builder::Intermediate,
    };
    use p256::ecdsa::SigningKey;
    use std::{convert::TryInto, str::FromStr};

    pub const RAW_PRIVATE_KEY_FOR_TEST: &str = include_str!("testing_keys/test_private_key.pem");
    pub const RAW_PUBLIC_KEY_FOR_TEST: &str = include_str!("testing_keys/test_public_key.pem");

    pub fn make_default_public_key_id_for_test() -> PublicKeyId {
        123456789.try_into().unwrap()
    }
    pub fn make_default_private_key_for_test() -> SigningKey {
        SigningKey::from_str(RAW_PRIVATE_KEY_FOR_TEST).unwrap()
    }
    pub fn make_default_public_key_for_test() -> PublicKey {
        PublicKey::from_str(RAW_PUBLIC_KEY_FOR_TEST).unwrap()
    }

    pub fn make_keys_for_test() -> (SigningKey, PublicKey) {
        (make_default_private_key_for_test(), make_default_public_key_for_test())
    }

    pub fn make_public_keys_for_test(
        public_key_id: PublicKeyId,
        public_key: PublicKey,
    ) -> PublicKeys {
        PublicKeys {
            latest: PublicKeyAndId { id: public_key_id, key: public_key },
            historical: vec![],
        }
    }

    pub fn make_default_public_keys_for_test() -> PublicKeys {
        let (_priv_key, public_key) = make_keys_for_test();
        make_public_keys_for_test(make_default_public_key_id_for_test(), public_key)
    }

    pub fn make_default_json_public_keys_for_test() -> serde_json::Value {
        serde_json::json!({
            "latest": {
                "id": make_default_public_key_id_for_test(),
                "key": RAW_PUBLIC_KEY_FOR_TEST,
            },
            "historical": []
        })
    }
    pub fn make_cup_handler_for_test() -> StandardCupv2Handler {
        let (_signing_key, public_key) = make_keys_for_test();
        let public_keys =
            make_public_keys_for_test(make_default_public_key_id_for_test(), public_key);
        StandardCupv2Handler::new(&public_keys)
    }

    pub fn make_expected_signature_for_test(
        signing_key: &SigningKey,
        request_metadata: &RequestMetadata,
        response_body: &[u8],
    ) -> Vec<u8> {
        use signature::Signer;
        let transaction_hash = make_transaction_hash(
            &request_metadata.request_body,
            response_body,
            request_metadata.public_key_id,
            &request_metadata.nonce,
        );
        signing_key.sign(&transaction_hash).to_der().as_bytes().to_vec()
    }

    // Mock Cupv2Handler which can be used to fail at request decoration or verification.
    pub struct MockCupv2Handler {
        decoration_error: fn() -> Option<CupDecorationError>,
        verification_error: fn() -> Option<CupVerificationError>,
    }
    impl MockCupv2Handler {
        pub fn new() -> MockCupv2Handler {
            MockCupv2Handler {
                decoration_error: || None::<CupDecorationError>,
                verification_error: || None::<CupVerificationError>,
            }
        }
        pub fn set_decoration_error(
            mut self,
            e: fn() -> Option<CupDecorationError>,
        ) -> MockCupv2Handler {
            self.decoration_error = e;
            self
        }
        pub fn set_verification_error(
            mut self,
            e: fn() -> Option<CupVerificationError>,
        ) -> MockCupv2Handler {
            self.verification_error = e;
            self
        }
    }

    impl Default for MockCupv2Handler {
        fn default() -> Self {
            Self::new()
        }
    }

    impl Cupv2RequestHandler for MockCupv2Handler {
        fn decorate_request(
            &self,
            _request: &mut impl CupRequest,
        ) -> Result<RequestMetadata, CupDecorationError> {
            match (self.decoration_error)() {
                Some(e) => Err(e),
                None => Ok(RequestMetadata {
                    request_body: vec![],
                    public_key_id: 0.try_into().unwrap(),
                    nonce: [0u8; 32].into(),
                }),
            }
        }

        fn verify_response(
            &self,
            request_metadata: &RequestMetadata,
            resp: &Response<Vec<u8>>,
            public_key_id: PublicKeyId,
        ) -> Result<DerSignature, CupVerificationError> {
            use rand::rngs::OsRng;
            let signing_key = SigningKey::random(&mut OsRng);
            let signature = DerSignature::from_bytes(&make_expected_signature_for_test(
                &signing_key,
                request_metadata,
                resp.body(),
            ))
            .unwrap();
            let () = self.verify_response_with_signature(
                &signature,
                &request_metadata.request_body,
                resp.body(),
                public_key_id,
                &request_metadata.nonce,
            )?;
            Ok(signature)
        }
    }

    impl Cupv2Verifier for MockCupv2Handler {
        fn verify_response_with_signature(
            &self,
            _ecdsa_signature: &DerSignature,
            _request_body: &[u8],
            _response_body: &[u8],
            _public_key_id: PublicKeyId,
            _nonce: &Nonce,
        ) -> Result<(), CupVerificationError> {
            match (self.verification_error)() {
                Some(e) => Err(e),
                None => Ok(()),
            }
        }
    }

    pub fn make_standard_intermediate_for_test(request: Request) -> Intermediate {
        Intermediate {
            uri: "http://fuchsia.dev".to_string(),
            headers: [].into(),
            body: RequestWrapper { request },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        protocol::request::{Request, RequestWrapper},
        request_builder::Intermediate,
    };
    use assert_matches::assert_matches;
    use p256::ecdsa::SigningKey;
    use url::Url;

    // For testing only, it is useful to compute equality for CupVerificationError enums.
    impl PartialEq for CupVerificationError {
        fn eq(&self, other: &Self) -> bool {
            format!("{:?}", self) == format!("{:?}", other)
        }
    }

    #[test]
    fn test_standard_cup_handler_decorate() -> Result<(), anyhow::Error> {
        let (_, public_key) = test_support::make_keys_for_test();
        let public_key_id: PublicKeyId = 42.try_into()?;
        let public_keys = test_support::make_public_keys_for_test(public_key_id, public_key);
        let cup_handler = StandardCupv2Handler::new(&public_keys);

        let mut intermediate =
            test_support::make_standard_intermediate_for_test(Request::default());

        let request_metadata = cup_handler.decorate_request(&mut intermediate)?;

        // check cup2key value newly set on request.
        let cup2key_value: String = Url::parse(&intermediate.uri)?
            .query_pairs()
            .find_map(|(k, v)| if k == "cup2key" { Some(v) } else { None })
            .unwrap()
            .to_string();

        let (public_key_decimal, nonce_hex) = cup2key_value.split_once(':').unwrap();
        assert_eq!(public_key_decimal, public_key_id.to_string());
        assert_eq!(nonce_hex, request_metadata.nonce.to_string());
        // Assert that the nonce is being generated randomly inline (i.e. not the default value).
        assert_ne!(request_metadata.nonce, [0_u8; 32].into());

        Ok(())
    }

    #[test]
    fn test_standard_cup_handler_decorate_ipv6_link_local() -> Result<(), anyhow::Error> {
        let (_, public_key) = test_support::make_keys_for_test();
        let public_key_id: PublicKeyId = 42.try_into()?;
        let public_keys = test_support::make_public_keys_for_test(public_key_id, public_key);
        let cup_handler = StandardCupv2Handler::new(&public_keys);

        let mut intermediate = Intermediate {
            uri: "http://[::1%eth0]".to_string(),
            headers: [].into(),
            body: RequestWrapper { request: Request::default() },
        };

        let request_metadata = cup_handler.decorate_request(&mut intermediate)?;

        assert_eq!(
            intermediate.uri,
            format!("http://[::1%eth0]/?cup2key={}:{}", public_key_id, request_metadata.nonce,)
        );

        // Assert that the nonce is being generated randomly inline (i.e. not the default value).
        assert_ne!(request_metadata.nonce, [0_u8; 32].into());

        Ok(())
    }

    #[test]
    fn test_verify_response_missing_etag_header() -> Result<(), anyhow::Error> {
        let (_, public_key) = test_support::make_keys_for_test();
        let public_key_id: PublicKeyId = 12345.try_into()?;
        let public_keys = test_support::make_public_keys_for_test(public_key_id, public_key);
        let cup_handler = StandardCupv2Handler::new(&public_keys);

        let mut intermediate =
            test_support::make_standard_intermediate_for_test(Request::default());
        let request_metadata = cup_handler.decorate_request(&mut intermediate)?;

        // No .header(ETAG, <val>), which is a problem.
        let response: Response<Vec<u8>> =
            hyper::Response::builder().status(200).body("foo".as_bytes().to_vec())?;

        assert_matches!(
            cup_handler.verify_response(&request_metadata, &response, public_key_id),
            Err(CupVerificationError::EtagHeaderMissing)
        );
        Ok(())
    }

    #[test]
    fn test_verify_response_malformed_etag_header() -> Result<(), anyhow::Error> {
        let (_, public_key) = test_support::make_keys_for_test();
        let public_key_id: PublicKeyId = 12345.try_into()?;
        let public_keys = test_support::make_public_keys_for_test(public_key_id, public_key);
        let cup_handler = StandardCupv2Handler::new(&public_keys);

        let mut intermediate =
            test_support::make_standard_intermediate_for_test(Request::default());
        let request_metadata = cup_handler.decorate_request(&mut intermediate)?;

        let response: Response<Vec<u8>> = hyper::Response::builder()
            .status(200)
            .header(ETAG, "\u{FEFF}")
            .body("foo".as_bytes().to_vec())?;

        assert_matches!(
            cup_handler.verify_response(&request_metadata, &response, public_key_id),
            Err(CupVerificationError::EtagNotString(_))
        );
        Ok(())
    }

    #[test]
    fn test_verify_cached_signature_against_message() -> Result<(), anyhow::Error> {
        let (priv_key, public_key) = test_support::make_keys_for_test();
        let response_body = "bar";
        let correct_public_key_id: PublicKeyId = 24682468.try_into()?;
        let wrong_public_key_id: PublicKeyId = 12341234.try_into()?;

        let public_keys =
            test_support::make_public_keys_for_test(correct_public_key_id, public_key);
        let cup_handler = StandardCupv2Handler::new(&public_keys);
        let mut intermediate =
            test_support::make_standard_intermediate_for_test(Request::default());
        let request_metadata = cup_handler.decorate_request(&mut intermediate)?;
        let expected_request_metadata = RequestMetadata {
            request_body: intermediate.serialize_body()?,
            public_key_id: correct_public_key_id,
            nonce: request_metadata.nonce,
        };

        let expected_hash = Sha256::digest(&request_metadata.request_body);

        let expected_hash_hex: String = hex::encode(expected_hash);
        let expected_signature = hex::encode(test_support::make_expected_signature_for_test(
            &priv_key,
            &expected_request_metadata,
            response_body.as_bytes(),
        ));

        for (etag, public_key_id, expected_err) in vec![
            // This etag doesn't even have the form foo:bar.
            ("bar", correct_public_key_id, Some(CupVerificationError::EtagMalformed)),
            // This etag has the form foo:bar, but the latter isn't a real hash.
            ("foo:bar", correct_public_key_id, Some(CupVerificationError::RequestHashMalformed)),
            // This hash is the right length, but doesn't decode to the right value.
            (
                &format!("foo:{}", hex::encode([1; 32])),
                correct_public_key_id,
                Some(CupVerificationError::RequestHashMismatch),
            ),
            // The hash is the right length and the right value.
            // But the signature is malformed.
            (
                &format!("foo:{}", expected_hash_hex),
                correct_public_key_id,
                Some(CupVerificationError::SignatureMalformed),
            ),
            // The hash is the right length and the right value.
            // But the signature decodes to the wrong value.
            (
                &format!("{}:{}", hex::encode([1; 64]), expected_hash_hex),
                correct_public_key_id,
                Some(CupVerificationError::SignatureError(ecdsa::Error::new())),
            ),
            // Wrong public key ID.
            (
                &format!("{}:{}", expected_signature, expected_hash_hex,),
                wrong_public_key_id,
                Some(CupVerificationError::SpecifiedPublicKeyIdMissing),
            ),
            // Finally, the happy path.
            (
                &format!("{}:{}", expected_signature, expected_hash_hex,),
                correct_public_key_id,
                None,
            ),
        ] {
            let response: Response<Vec<u8>> = hyper::Response::builder()
                .status(200)
                .header(ETAG, etag)
                .body(response_body.as_bytes().to_vec())?;
            let actual_err =
                cup_handler.verify_response(&request_metadata, &response, public_key_id).err();
            assert_eq!(
                actual_err, expected_err,
                "Received error {:?}, expected error {:?}",
                actual_err, expected_err
            );
        }

        Ok(())
    }

    // Helper method which, given some setup arguments, can produce a valid
    // matching request metadata hash and response. Used in historical
    // verification below.
    fn make_verify_response_arguments(
        request_handler: &impl Cupv2RequestHandler,
        private_key: SigningKey,
        response_body: &str,
    ) -> Result<(RequestMetadata, Response<Vec<u8>>), anyhow::Error> {
        let mut intermediate =
            test_support::make_standard_intermediate_for_test(Request::default());
        let request_metadata = request_handler.decorate_request(&mut intermediate)?;

        let signature = hex::encode(test_support::make_expected_signature_for_test(
            &private_key,
            &request_metadata,
            response_body.as_bytes(),
        ));

        let etag = &format!(
            "{}:{}",
            signature,
            hex::encode(Sha256::digest(&request_metadata.request_body))
        );

        let response: Response<Vec<u8>> = hyper::Response::builder()
            .status(200)
            .header(ETAG, etag)
            .body(response_body.as_bytes().to_vec())?;
        Ok((request_metadata, response))
    }

    #[test]
    fn test_historical_verification() -> Result<(), anyhow::Error> {
        let (private_key_a, public_key_a) = test_support::make_keys_for_test();
        let public_key_id_a: PublicKeyId = 24682468.try_into()?;
        let response_body_a = "foo";

        let public_keys = PublicKeys {
            latest: PublicKeyAndId { id: public_key_id_a, key: public_key_a },
            historical: vec![],
        };
        let mut cup_handler = StandardCupv2Handler::new(&public_keys);
        let (request_metadata_a, response_a) =
            make_verify_response_arguments(&cup_handler, private_key_a, response_body_a)?;
        assert_matches!(
            cup_handler.verify_response(&request_metadata_a, &response_a, public_key_id_a),
            Ok(_)
        );

        // Now introduce a new set of keys,
        let (private_key_b, public_key_b) = test_support::make_keys_for_test();
        let public_key_id_b: PublicKeyId = 12341234.try_into()?;
        let response_body_b = "bar";

        // and redefine the cuphandler with new keys and knowledge of historical keys.
        let public_keys = PublicKeys {
            latest: PublicKeyAndId { id: public_key_id_b, key: public_key_b },
            historical: vec![PublicKeyAndId { id: public_key_id_a, key: public_key_a }],
        };
        cup_handler = StandardCupv2Handler::new(&public_keys);

        let (request_metadata_b, response_b) =
            make_verify_response_arguments(&cup_handler, private_key_b, response_body_b)?;
        // and verify that the cup handler can verify a newly generated response,
        assert_matches!(
            cup_handler.verify_response(&request_metadata_b, &response_b, public_key_id_b),
            Ok(_)
        );

        // as well as a response which has already been generated and stored.
        assert_matches!(
            cup_handler.verify_response(&request_metadata_a, &response_a, public_key_id_a),
            Ok(_)
        );

        // finally, assert that verification fails if either (1) the hash, (2)
        // the stored response, or (3) the key ID itself is wrong.
        assert!(cup_handler
            .verify_response(&request_metadata_a, &response_a, public_key_id_b)
            .is_err());
        assert!(cup_handler
            .verify_response(&request_metadata_a, &response_b, public_key_id_a)
            .is_err());
        assert!(cup_handler
            .verify_response(&request_metadata_b, &response_a, public_key_id_a)
            .is_err());

        Ok(())
    }

    #[test]
    fn test_deserialize_public_keys() {
        let public_key_and_id: PublicKeyAndId = serde_json::from_value(serde_json::json!(
            {
                 "id": 123,
                 "key": test_support::RAW_PUBLIC_KEY_FOR_TEST,
            }
        ))
        .unwrap();

        assert_eq!(public_key_and_id.key, test_support::make_default_public_key_for_test());
    }

    #[test]
    fn test_publickeys_roundtrip() {
        // Test that serializing and deserializing a PublicKeys struct using
        // from_pem / to_pem results in the same struct.
        let public_keys = test_support::make_default_public_keys_for_test();
        let public_keys_serialized = serde_json::to_string(&public_keys).unwrap();
        let public_keys_reconstituted = serde_json::from_str(&public_keys_serialized).unwrap();
        assert_eq!(public_keys, public_keys_reconstituted);
    }

    #[test]
    fn test_parse_etag() {
        // W/ prefix
        assert_eq!(parse_etag("W/\"foo\""), "foo");
        assert_eq!(parse_etag("W/\"thing-\"with\"-quotes\""), "thing-\"with\"-quotes");
        assert_eq!(parse_etag("W/\"\""), "");
        // only surrounding quotes
        assert_eq!(parse_etag("\"foo\""), "foo");
        assert_eq!(parse_etag("\"thing-\"with\"-quotes\""), "thing-\"with\"-quotes",);
        assert_eq!(parse_etag("\"\""), "");
        // otherwise, left unchanged
        for v in ["foo", "1", "W", "W\"", "W/\"", "W/", "w/\"bar\"", "W/'bar'", ""] {
            //
            assert_eq!(parse_etag(v), v);
        }
    }
}
