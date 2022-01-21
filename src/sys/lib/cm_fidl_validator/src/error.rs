// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {std::fmt, thiserror::Error};

/// Enum type that can represent any error encountered during validation.
#[derive(Debug, Error, PartialEq, Clone)]
pub enum Error {
    #[error("{} missing {}", .0.decl, .0.field)]
    MissingField(DeclField),
    #[error("{} has empty {}", .0.decl, .0.field)]
    EmptyField(DeclField),
    #[error("{} has extraneous {}", .0.decl, .0.field)]
    ExtraneousField(DeclField),
    #[error("\"{1}\" is a duplicate {} {}", .0.decl, .0.field)]
    DuplicateField(DeclField, String),
    #[error("{} has invalid {}", .0.decl, .0.field)]
    InvalidField(DeclField),
    #[error("{}'s {} is too long", .0.decl, .0.field)]
    FieldTooLong(DeclField),
    #[error("\"{0}\" cannot declare a capability of type {1}")]
    InvalidCapabilityType(DeclField, String),
    #[error("\"{0}\" target \"{1}\" is same as source")]
    OfferTargetEqualsSource(String, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in children")]
    InvalidChild(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in collections")]
    InvalidCollection(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in storage")]
    InvalidStorage(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in environments")]
    InvalidEnvironment(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in capabilities")]
    InvalidCapability(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in runners")]
    InvalidRunner(DeclField, String),
    #[error("\"{1}\" is referenced in {0} but it does not appear in events")]
    EventStreamEventNotFound(DeclField, String),
    #[error("Event \"{1}\" is referenced in {0} with unsupported mode \"{2}\"")]
    EventStreamUnsupportedMode(DeclField, String, String),
    #[error("dependency cycle(s) exist: {0}")]
    DependencyCycle(String),
    #[error("{} \"{}\" path overlaps with {} \"{}\"", decl, path, other_decl, other_path)]
    InvalidPathOverlap { decl: DeclField, path: String, other_decl: DeclField, other_path: String },
    #[error("built-in capability decl {0} should not specify a source path, found \"{1}\"")]
    ExtraneousSourcePath(DeclField, String),
    #[error("configuration schema defines a vector nested in another vector")]
    NestedVector,
}

impl Error {
    pub fn missing_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::MissingField(DeclField { decl: decl_type.into(), field: keyword.into() })
    }

    pub fn empty_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::EmptyField(DeclField { decl: decl_type.into(), field: keyword.into() })
    }

    pub fn extraneous_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::ExtraneousField(DeclField { decl: decl_type.into(), field: keyword.into() })
    }

    pub fn duplicate_field(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        value: impl Into<String>,
    ) -> Self {
        Error::DuplicateField(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            value.into(),
        )
    }

    pub fn invalid_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::InvalidField(DeclField { decl: decl_type.into(), field: keyword.into() })
    }

    pub fn field_too_long(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::FieldTooLong(DeclField { decl: decl_type.into(), field: keyword.into() })
    }

    pub fn invalid_capability_type(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        type_name: impl Into<String>,
    ) -> Self {
        Error::InvalidCapabilityType(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            type_name.into(),
        )
    }

    pub fn offer_target_equals_source(decl: impl Into<String>, target: impl Into<String>) -> Self {
        Error::OfferTargetEqualsSource(decl.into(), target.into())
    }

    pub fn invalid_child(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        child: impl Into<String>,
    ) -> Self {
        Error::InvalidChild(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            child.into(),
        )
    }

    pub fn invalid_collection(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        collection: impl Into<String>,
    ) -> Self {
        Error::InvalidCollection(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            collection.into(),
        )
    }

    pub fn invalid_storage(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        storage: impl Into<String>,
    ) -> Self {
        Error::InvalidStorage(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            storage.into(),
        )
    }

    pub fn invalid_environment(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        environment: impl Into<String>,
    ) -> Self {
        Error::InvalidEnvironment(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            environment.into(),
        )
    }

    // TODO: Replace with `invalid_capability`?
    pub fn invalid_runner(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        runner: impl Into<String>,
    ) -> Self {
        Error::InvalidRunner(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            runner.into(),
        )
    }

    pub fn invalid_capability(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        capability: impl Into<String>,
    ) -> Self {
        Error::InvalidCapability(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            capability.into(),
        )
    }

    pub fn event_stream_event_not_found(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        event_name: impl Into<String>,
    ) -> Self {
        Error::EventStreamEventNotFound(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            event_name.into(),
        )
    }

    pub fn event_stream_unsupported_mode(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        event_name: impl Into<String>,
        event_mode: impl Into<String>,
    ) -> Self {
        Error::EventStreamUnsupportedMode(
            DeclField { decl: decl_type.into(), field: keyword.into() },
            event_name.into(),
            event_mode.into(),
        )
    }

    pub fn dependency_cycle(error: String) -> Self {
        Error::DependencyCycle(error)
    }

    pub fn invalid_path_overlap(
        decl: impl Into<String>,
        path: impl Into<String>,
        other_decl: impl Into<String>,
        other_path: impl Into<String>,
    ) -> Self {
        Error::InvalidPathOverlap {
            decl: DeclField { decl: decl.into(), field: "target_path".to_string() },
            path: path.into(),
            other_decl: DeclField { decl: other_decl.into(), field: "target_path".to_string() },
            other_path: other_path.into(),
        }
    }

    pub fn extraneous_source_path(decl_type: impl Into<String>, path: impl Into<String>) -> Self {
        Error::ExtraneousSourcePath(
            DeclField { decl: decl_type.into(), field: "source_path".to_string() },
            path.into(),
        )
    }

    pub fn nested_vector() -> Self {
        Error::NestedVector
    }
}

#[derive(Debug, PartialEq, Clone)]
pub struct DeclField {
    pub decl: String,
    pub field: String,
}

impl fmt::Display for DeclField {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}.{}", &self.decl, &self.field)
    }
}

/// Represents a list of errors encountered during validation.
#[derive(Debug, Error, PartialEq, Clone)]
pub struct ErrorList {
    pub errs: Vec<Error>,
}

impl ErrorList {
    pub(crate) fn new(errs: Vec<Error>) -> ErrorList {
        ErrorList { errs }
    }
}

impl fmt::Display for ErrorList {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let strs: Vec<String> = self.errs.iter().map(|e| format!("{}", e)).collect();
        write!(f, "{}", strs.join(", "))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_errors() {
        assert_eq!(format!("{}", Error::missing_field("Decl", "keyword")), "Decl missing keyword");
        assert_eq!(format!("{}", Error::empty_field("Decl", "keyword")), "Decl has empty keyword");
        assert_eq!(
            format!("{}", Error::duplicate_field("Decl", "keyword", "foo")),
            "\"foo\" is a duplicate Decl keyword"
        );
        assert_eq!(
            format!("{}", Error::invalid_field("Decl", "keyword")),
            "Decl has invalid keyword"
        );
        assert_eq!(
            format!("{}", Error::field_too_long("Decl", "keyword")),
            "Decl's keyword is too long"
        );
        assert_eq!(
            format!("{}", Error::invalid_child("Decl", "source", "child")),
            "\"child\" is referenced in Decl.source but it does not appear in children"
        );
        assert_eq!(
            format!("{}", Error::invalid_collection("Decl", "source", "child")),
            "\"child\" is referenced in Decl.source but it does not appear in collections"
        );
        assert_eq!(
            format!("{}", Error::invalid_storage("Decl", "source", "name")),
            "\"name\" is referenced in Decl.source but it does not appear in storage"
        );
    }
}
