// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use http::Response;
use hyper::header::ETAG;
use p256::ecdsa::{signature::Verifier as _, DerSignature, VerifyingKey};
use rand::{thread_rng, Rng};
use sha2::{Digest, Sha256};
use signature::Signature;
use std::{collections::HashMap, convert::TryInto, fmt::Debug};
use url::Url;

/// Error enum listing different kinds of CUPv2 decoration errors.
#[derive(Debug, thiserror::Error)]
pub enum CupDecorationError {
    #[error("could not serialize request.")]
    SerializationError(#[from] serde_json::Error),
    #[error("could not parse existing URI.")]
    ParseError(#[from] url::ParseError),
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

/// Error enum listing different kinds of CUPv2 constructor errors.
#[derive(Debug, thiserror::Error)]
pub enum CupConstructorError {
    #[error("inputs must have at least one (public key id, public key) pair.")]
    KeysMissing,
    #[error("Specified latest public key not found in input map.")]
    LatestPublicKeyNotFound,
}

/// By convention, this is always the u64 hash of the public key
/// value.
pub type PublicKeyId = u64;

pub type Nonce = [u8; 32];

/// Request decoration return type, containing request internals. Clients of this
/// library can call .hash() and store/retrieve the hash, or they can inspect the
/// request, public key ID, nonce used if necessary.
pub struct RequestMetadata {
    #[allow(dead_code)]
    request_body: Vec<u8>,
    #[allow(dead_code)]
    public_key_id: PublicKeyId,
    #[allow(dead_code)]
    nonce: Nonce,
}
impl RequestMetadata {
    #[allow(dead_code)]
    fn hash(&self) -> Vec<u8> {
        let mut hasher = Sha256::new();
        hasher.update(&self.request_body);
        hasher.update(self.public_key_id.to_string().as_bytes());
        hasher.update(hex::encode(self.nonce).as_bytes());
        hasher.finalize().as_slice().to_vec()
    }
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
// requests. Can also be used to verify the stored signature of a CUPv2 request
// after-the-fact; verify_response_with_signature is not hyper-aware.
pub trait Cupv2Handler {
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
        request_metadata_hash: &[u8],
        resp: &Response<Vec<u8>>,
        public_key_id: PublicKeyId,
    ) -> Result<(), CupVerificationError>;

    /// The same behavior as verify_response, but designed for verifying stored
    /// signatures which are not hyper-aware.
    fn verify_response_with_signature(
        &self,
        ecdsa_signature: DerSignature,
        request_metadata_hash: &[u8],
        response_body: &[u8],
        public_key_id: PublicKeyId,
    ) -> Result<(), CupVerificationError>;
}

// Default Cupv2Handler.
pub struct StandardCupv2Handler {
    /// Device-wide map from public key ID# to public key. Should be ingested at
    /// startup. This map should never be empty.
    parameters_by_id: HashMap<PublicKeyId, VerifyingKey>,
    latest_public_key_id: PublicKeyId,
}

impl StandardCupv2Handler {
    /// Constructor for the standard CUPv2 handler. Accepts a map from
    /// PublicKeyId to PublicKey, as well as a latest PublicKeyId to use when
    /// decorating requests.
    pub fn new(
        parameters_by_id: impl Into<HashMap<PublicKeyId, VerifyingKey>>,
        latest_public_key_id: PublicKeyId,
    ) -> Result<Self, CupConstructorError> {
        let parameters_by_id: HashMap<PublicKeyId, VerifyingKey> = parameters_by_id.into();
        if parameters_by_id.is_empty() {
            return Err(CupConstructorError::KeysMissing);
        }
        if parameters_by_id.get(&latest_public_key_id).is_none() {
            return Err(CupConstructorError::LatestPublicKeyNotFound);
        }
        Ok(Self { parameters_by_id, latest_public_key_id })
    }
}

impl Cupv2Handler for StandardCupv2Handler {
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

        let mut nonce: Nonce = [0_u8; 32];
        thread_rng().fill(&mut nonce[..]);

        request.set_uri(
            Url::parse_with_params(
                request.get_uri(),
                &[("cup2key", format!("{}:{}", public_key_id, hex::encode(nonce)))],
            )?
            .into_string(),
        );

        Ok(RequestMetadata { request_body: request.get_serialized_body()?, public_key_id, nonce })
    }

    fn verify_response(
        &self,
        request_metadata_hash: &[u8],
        resp: &Response<Vec<u8>>,
        public_key_id: PublicKeyId,
    ) -> Result<(), CupVerificationError> {
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

        let etag_header: &str = resp
            .headers()
            .get(ETAG)
            .ok_or(CupVerificationError::EtagHeaderMissing)?
            .to_str()
            .map_err(CupVerificationError::EtagNotString)?;

        let (encoded_signature, hex_hash): (&str, &str) =
            etag_header.split_once(':').ok_or(CupVerificationError::EtagMalformed)?;

        let actual_hash =
            &hex::decode(hex_hash).map_err(|_| CupVerificationError::RequestHashMalformed)?;

        if request_metadata_hash != actual_hash {
            return Err(CupVerificationError::RequestHashMismatch);
        }

        let signature = DerSignature::from_bytes(
            &hex::decode(encoded_signature)
                .map_err(|_| CupVerificationError::SignatureMalformed)?,
        )?;

        self.verify_response_with_signature(
            signature,
            request_metadata_hash,
            resp.body(),
            public_key_id,
        )
    }

    fn verify_response_with_signature(
        &self,
        ecdsa_signature: DerSignature,
        request_metadata_hash: &[u8],
        response_body: &[u8],
        public_key_id: PublicKeyId,
    ) -> Result<(), CupVerificationError> {
        let mut message: Vec<u8> = response_body.to_vec();
        message.extend(request_metadata_hash);

        let key: &VerifyingKey = self
            .parameters_by_id
            .get(&public_key_id)
            .ok_or(CupVerificationError::SpecifiedPublicKeyIdMissing)?;
        Ok(key.verify(&message, &ecdsa_signature.try_into()?)?)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::protocol::request::{Request, RequestWrapper};
    use crate::request_builder::Intermediate;
    use assert_matches::assert_matches;
    use p256::ecdsa::SigningKey;
    use signature::rand_core::OsRng;
    use std::convert::TryInto;

    impl CupRequest for Intermediate {
        fn get_uri(&self) -> &str {
            &self.uri
        }
        fn set_uri(&mut self, uri: String) {
            self.uri = uri;
        }
        fn get_serialized_body(&self) -> serde_json::Result<Vec<u8>> {
            self.serialize_body()
        }
    }

    // For testing only, it is useful to compute equality for CupVerificationError enums.
    impl PartialEq for CupVerificationError {
        fn eq(&self, other: &Self) -> bool {
            format!("{:?}", self) == format!("{:?}", other)
        }
    }

    fn make_standard_intermediate_for_test(request: Request) -> Intermediate {
        Intermediate {
            uri: "http://fuchsia.dev".to_string(),
            headers: [].into(),
            body: RequestWrapper { request },
        }
    }

    fn make_standard_cup_handler_for_test(
        public_key_id: PublicKeyId,
        verifying_key: VerifyingKey,
    ) -> StandardCupv2Handler {
        StandardCupv2Handler::new([(public_key_id, verifying_key)], public_key_id).unwrap()
    }

    fn make_expected_signature_for_test(
        signing_key: &SigningKey,
        request_metadata: &RequestMetadata,
        response_body: &[u8],
    ) -> String {
        use signature::Signer;
        let mut message: Vec<u8> = response_body.to_vec();
        message.extend(request_metadata.hash());
        hex::encode(signing_key.sign(&message).to_der().as_bytes())
    }

    fn make_keys_for_test() -> (SigningKey, VerifyingKey) {
        let signing_key = SigningKey::random(&mut OsRng);
        let verifying_key = VerifyingKey::from(&signing_key);
        (signing_key, verifying_key)
    }

    fn make_expected_hash_for_test(
        intermediate: &Intermediate,
        nonce: Nonce,
        public_key_id: impl std::string::ToString,
    ) -> Vec<u8> {
        let mut hasher = Sha256::new();
        hasher.update(&intermediate.serialize_body().unwrap());
        hasher.update(public_key_id.to_string().as_bytes());
        hasher.update(hex::encode(nonce).as_bytes());
        hasher.finalize().as_slice().to_vec()
    }

    #[test]
    fn test_standard_cup_handler_decorate() -> Result<(), anyhow::Error> {
        let (_, verifying_key) = make_keys_for_test();
        let public_key_id: PublicKeyId = 42.try_into()?;
        let cup_handler =
            StandardCupv2Handler::new([(public_key_id, verifying_key)], public_key_id)?;

        let mut intermediate = make_standard_intermediate_for_test(Request::default());

        let request_metadata = cup_handler.decorate_request(&mut intermediate)?;

        // check cup2key value newly set on request.
        let cup2key_value: String = Url::parse(&intermediate.uri)?
            .query_pairs()
            .find_map(|(k, v)| if k == "cup2key" { Some(v) } else { None })
            .unwrap()
            .to_string();

        let (public_key_decimal, nonce_hex) = cup2key_value.split_once(':').unwrap();
        assert_eq!(public_key_decimal, public_key_id.to_string());
        assert_eq!(nonce_hex, hex::encode(request_metadata.nonce));
        // Assert that the nonce is being generated randomly inline (i.e. not the default value).
        assert_ne!(request_metadata.nonce, [0_u8; 32]);

        Ok(())
    }

    #[test]
    fn test_verify_response_missing_etag_header() -> Result<(), anyhow::Error> {
        let (_, verifying_key) = make_keys_for_test();
        let public_key_id: PublicKeyId = 12345.try_into()?;
        let cup_handler = make_standard_cup_handler_for_test(public_key_id, verifying_key);

        let mut intermediate = make_standard_intermediate_for_test(Request::default());
        let request_metadata = cup_handler.decorate_request(&mut intermediate)?;

        // No .header(ETAG, <val>), which is a problem.
        let response: Response<Vec<u8>> =
            hyper::Response::builder().status(200).body("foo".as_bytes().to_vec())?;

        assert_matches!(
            cup_handler.verify_response(&request_metadata.hash(), &response, public_key_id),
            Err(CupVerificationError::EtagHeaderMissing)
        );
        Ok(())
    }

    #[test]
    fn test_verify_response_malformed_etag_header() -> Result<(), anyhow::Error> {
        let (_, verifying_key) = make_keys_for_test();
        let public_key_id: PublicKeyId = 12345.try_into()?;
        let cup_handler = make_standard_cup_handler_for_test(public_key_id, verifying_key);

        let mut intermediate = make_standard_intermediate_for_test(Request::default());
        let request_metadata = cup_handler.decorate_request(&mut intermediate)?;

        let response: Response<Vec<u8>> = hyper::Response::builder()
            .status(200)
            .header(ETAG, "\u{FEFF}")
            .body("foo".as_bytes().to_vec())?;

        assert_matches!(
            cup_handler.verify_response(&request_metadata.hash(), &response, public_key_id),
            Err(CupVerificationError::EtagNotString(_))
        );
        Ok(())
    }

    #[test]
    fn test_verify_cached_signature_against_message() -> Result<(), anyhow::Error> {
        let (priv_key, verifying_key) = make_keys_for_test();
        let response_body = "bar";
        let correct_public_key_id: PublicKeyId = 24682468.try_into()?;
        let wrong_public_key_id: PublicKeyId = 12341234.try_into()?;

        let cup_handler = StandardCupv2Handler::new(
            [(correct_public_key_id, verifying_key)],
            correct_public_key_id,
        )?;
        let mut intermediate = make_standard_intermediate_for_test(Request::default());
        let request_metadata = cup_handler.decorate_request(&mut intermediate)?;
        let expected_request_metadata = RequestMetadata {
            request_body: intermediate.serialize_body()?,
            public_key_id: correct_public_key_id,
            nonce: request_metadata.nonce,
        };

        let expected_hash = make_expected_hash_for_test(
            &intermediate,
            request_metadata.nonce,
            correct_public_key_id,
        );
        let expected_hash_hex: String = hex::encode(expected_hash);
        let expected_signature = make_expected_signature_for_test(
            &priv_key,
            &expected_request_metadata,
            response_body.as_bytes(),
        );

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
            let actual_err = cup_handler
                .verify_response(&request_metadata.hash(), &response, public_key_id)
                .err();
            assert_eq!(
                actual_err, expected_err,
                "Received error {:?}, expected error {:?}",
                actual_err, expected_err
            );
        }

        Ok(())
    }

    #[test]
    fn test_standard_cup_handler_ctor_failure() -> Result<(), anyhow::Error> {
        let public_key_id: PublicKeyId = 24682468.try_into()?;
        assert_matches!(
            StandardCupv2Handler::new([], public_key_id).err(),
            Some(CupConstructorError::KeysMissing)
        );
        Ok(())
    }
}
