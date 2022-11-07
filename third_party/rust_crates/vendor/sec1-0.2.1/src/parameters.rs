use der::{
    asn1::{Any, ObjectIdentifier},
    DecodeValue, Decoder, EncodeValue, Encoder, FixedTag, Length, Tag,
};

/// Elliptic curve parameters as described in
/// [RFC5480 Section 2.1.1](https://datatracker.ietf.org/doc/html/rfc5480#section-2.1.1):
///
/// ```text
/// ECParameters ::= CHOICE {
///   namedCurve         OBJECT IDENTIFIER
///   -- implicitCurve   NULL
///   -- specifiedCurve  SpecifiedECDomain
/// }
///   -- implicitCurve and specifiedCurve MUST NOT be used in PKIX.
///   -- Details for SpecifiedECDomain can be found in [X9.62].
///   -- Any future additions to this CHOICE should be coordinated
///   -- with ANSI X9.
/// ```
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum EcParameters {
    /// Elliptic curve named by a particular OID.
    ///
    /// > namedCurve identifies all the required values for a particular
    /// > set of elliptic curve domain parameters to be represented by an
    /// > object identifier.
    NamedCurve(ObjectIdentifier),
}

impl DecodeValue<'_> for EcParameters {
    fn decode_value(decoder: &mut Decoder<'_>, length: Length) -> der::Result<Self> {
        ObjectIdentifier::decode_value(decoder, length).map(Self::NamedCurve)
    }
}

impl EncodeValue for EcParameters {
    fn value_len(&self) -> der::Result<Length> {
        match self {
            Self::NamedCurve(oid) => oid.value_len(),
        }
    }

    fn encode_value(&self, encoder: &mut Encoder<'_>) -> der::Result<()> {
        match self {
            Self::NamedCurve(oid) => oid.encode_value(encoder),
        }
    }
}

impl EcParameters {
    /// Obtain the `namedCurve` OID.
    pub fn named_curve(self) -> Option<ObjectIdentifier> {
        match self {
            Self::NamedCurve(oid) => Some(oid),
        }
    }
}

impl<'a> From<&'a EcParameters> for Any<'a> {
    fn from(params: &'a EcParameters) -> Any<'a> {
        match params {
            EcParameters::NamedCurve(oid) => oid.into(),
        }
    }
}

impl From<ObjectIdentifier> for EcParameters {
    fn from(oid: ObjectIdentifier) -> EcParameters {
        EcParameters::NamedCurve(oid)
    }
}

impl FixedTag for EcParameters {
    const TAG: Tag = Tag::ObjectIdentifier;
}
