//! SEC1 EC private key document.

use crate::{DecodeEcPrivateKey, EcPrivateKey, EncodeEcPrivateKey, Error, Result};
use alloc::vec::Vec;
use core::fmt;
use der::{Decodable, Document, Encodable};
use zeroize::{Zeroize, Zeroizing};

#[cfg(feature = "pem")]
use {
    crate::{pem, LineEnding},
    alloc::string::String,
    core::str::FromStr,
};

#[cfg(feature = "std")]
use std::{path::Path, str};

/// SEC1 `EC PRIVATE KEY` document.
///
/// This type provides storage for [`EcPrivateKey`] encoded as ASN.1 DER
/// with the invariant that the contained-document is "well-formed", i.e. it
/// will parse successfully according to this crate's parsing rules.
#[derive(Clone)]
#[cfg_attr(docsrs, doc(cfg(feature = "alloc")))]
pub struct EcPrivateKeyDocument(Zeroizing<Vec<u8>>);

impl<'a> Document<'a> for EcPrivateKeyDocument {
    type Message = EcPrivateKey<'a>;
    const SENSITIVE: bool = true;
}

impl DecodeEcPrivateKey for EcPrivateKeyDocument {
    fn from_sec1_der(bytes: &[u8]) -> Result<Self> {
        Ok(Self::from_der(bytes)?)
    }

    #[cfg(feature = "pem")]
    #[cfg_attr(docsrs, doc(cfg(feature = "pem")))]
    fn from_sec1_pem(s: &str) -> Result<Self> {
        Ok(Self::from_pem(s)?)
    }

    #[cfg(feature = "std")]
    #[cfg_attr(docsrs, doc(cfg(feature = "std")))]
    fn read_sec1_der_file(path: impl AsRef<Path>) -> Result<Self> {
        Ok(Self::read_der_file(path)?)
    }

    #[cfg(all(feature = "pem", feature = "std"))]
    #[cfg_attr(docsrs, doc(cfg(feature = "pem")))]
    #[cfg_attr(docsrs, doc(cfg(feature = "std")))]
    fn read_sec1_pem_file(path: impl AsRef<Path>) -> Result<Self> {
        Ok(Self::read_pem_file(path)?)
    }
}

impl EncodeEcPrivateKey for EcPrivateKeyDocument {
    fn to_sec1_der(&self) -> Result<EcPrivateKeyDocument> {
        Ok(self.clone())
    }

    #[cfg(feature = "pem")]
    #[cfg_attr(docsrs, doc(cfg(feature = "pem")))]
    fn to_sec1_pem(&self, line_ending: LineEnding) -> Result<Zeroizing<String>> {
        Ok(Zeroizing::new(self.to_pem(line_ending)?))
    }

    #[cfg(feature = "std")]
    #[cfg_attr(docsrs, doc(cfg(feature = "std")))]
    fn write_sec1_der_file(&self, path: impl AsRef<Path>) -> Result<()> {
        Ok(self.write_der_file(path)?)
    }

    #[cfg(all(feature = "pem", feature = "std"))]
    #[cfg_attr(docsrs, doc(cfg(feature = "pem")))]
    #[cfg_attr(docsrs, doc(cfg(feature = "std")))]
    fn write_sec1_pem_file(&self, path: impl AsRef<Path>, line_ending: LineEnding) -> Result<()> {
        Ok(self.write_pem_file(path, line_ending)?)
    }
}

impl AsRef<[u8]> for EcPrivateKeyDocument {
    fn as_ref(&self) -> &[u8] {
        self.0.as_ref()
    }
}

impl TryFrom<&[u8]> for EcPrivateKeyDocument {
    type Error = Error;

    fn try_from(bytes: &[u8]) -> Result<Self> {
        Ok(Self::from_der(bytes)?)
    }
}

impl TryFrom<EcPrivateKey<'_>> for EcPrivateKeyDocument {
    type Error = Error;

    fn try_from(private_key: EcPrivateKey<'_>) -> Result<Self> {
        Self::try_from(&private_key)
    }
}

impl TryFrom<&EcPrivateKey<'_>> for EcPrivateKeyDocument {
    type Error = Error;

    fn try_from(private_key: &EcPrivateKey<'_>) -> Result<Self> {
        Ok(Self(Zeroizing::new(private_key.to_vec()?)))
    }
}

impl TryFrom<Vec<u8>> for EcPrivateKeyDocument {
    type Error = der::Error;

    fn try_from(mut bytes: Vec<u8>) -> der::Result<Self> {
        // Ensure document is well-formed
        if let Err(err) = EcPrivateKey::from_der(bytes.as_slice()) {
            bytes.zeroize();
            return Err(err);
        }

        Ok(Self(Zeroizing::new(bytes)))
    }
}

impl fmt::Debug for EcPrivateKeyDocument {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt.debug_tuple("EcPrivateKeyDocument")
            .field(&self.decode())
            .finish()
    }
}

#[cfg(feature = "pem")]
#[cfg_attr(docsrs, doc(cfg(feature = "pem")))]
impl FromStr for EcPrivateKeyDocument {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self> {
        Self::from_sec1_pem(s)
    }
}

#[cfg(feature = "pem")]
#[cfg_attr(docsrs, doc(cfg(feature = "pem")))]
impl pem::PemLabel for EcPrivateKeyDocument {
    const TYPE_LABEL: &'static str = "EC PRIVATE KEY";
}
