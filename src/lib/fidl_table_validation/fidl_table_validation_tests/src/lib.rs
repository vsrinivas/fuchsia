// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_table_validation::{ValidFidlTable, Validate};
use std::convert::TryFrom;

#[test]
fn rejects_missing_fields() {
    struct FidlHello {
        required: Option<usize>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(FidlHello)]
    struct ValidHello {
        #[fidl_field_type(required)]
        required: usize,
    }

    assert_eq!(
        ValidHello::try_from(FidlHello { required: Some(10) }).expect("validation"),
        ValidHello { required: 10 },
    );

    match ValidHello::try_from(FidlHello { required: None }) {
        Err(FidlHelloValidationError::MissingField(FidlHelloMissingFieldError::Required)) => {}
        _ => panic!("Should have generated an error for missing required field."),
    };
}

#[test]
fn sets_default_fields() {
    struct FidlHello {
        has_default: Option<usize>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(FidlHello)]
    struct ValidHello {
        #[fidl_field_type(default = 22)]
        has_default: usize,
    }

    match ValidHello::try_from(FidlHello { has_default: None }) {
        Ok(ValidHello { has_default: 22 }) => {}
        _ => panic!("Expected successful validation with default value."),
    };
}

#[test]
fn accepts_optional_fields() {
    struct FidlHello {
        optional: Option<usize>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(FidlHello)]
    struct ValidHello {
        #[fidl_field_type(optional)]
        optional: Option<usize>,
    }

    assert_eq!(
        ValidHello::try_from(FidlHello { optional: None }).expect("validation"),
        ValidHello { optional: None }
    );

    assert_eq!(
        ValidHello::try_from(FidlHello { optional: Some(15) }).expect("validation"),
        ValidHello { optional: Some(15) }
    );
}

#[test]
fn invalid_fails_custom_validator() {
    struct FidlHello {
        should_not_be_12: Option<usize>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(FidlHello)]
    #[fidl_table_validator(FidlHelloValidator)]
    pub struct ValidHello {
        #[fidl_field_type(default = 10)]
        should_not_be_12: usize,
    }

    pub struct FidlHelloValidator;

    impl Validate<ValidHello> for FidlHelloValidator {
        type Error = ();
        fn validate(candidate: &ValidHello) -> Result<(), Self::Error> {
            match candidate.should_not_be_12 {
                12 => Err(()),
                _ => Ok(()),
            }
        }
    }

    match ValidHello::try_from(FidlHello { should_not_be_12: Some(12) }) {
        Err(FidlHelloValidationError::Logical(())) => {}
        _ => panic!("Wanted error from custom validator."),
    }
}

#[test]
fn valid_passes_custom_validator() {
    struct FidlHello {
        should_not_be_12: Option<usize>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(FidlHello)]
    #[fidl_table_validator(FidlHelloValidator)]
    pub struct ValidHello {
        #[fidl_field_type(default = 10)]
        should_not_be_12: usize,
    }

    pub struct FidlHelloValidator;

    impl Validate<ValidHello> for FidlHelloValidator {
        type Error = ();
        fn validate(candidate: &ValidHello) -> Result<(), Self::Error> {
            match candidate.should_not_be_12 {
                12 => Err(()),
                _ => Ok(()),
            }
        }
    }

    assert_eq!(
        ValidHello::try_from(FidlHello { should_not_be_12: None }).expect("validation"),
        ValidHello { should_not_be_12: 10 }
    );
}

#[test]
fn nested_valid_field_accepted() {
    struct NestedFidl {
        required: Option<usize>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(NestedFidl)]
    struct ValidNestedFidl {
        required: usize,
    }

    struct FidlHello {
        nested: Option<NestedFidl>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(FidlHello)]
    struct ValidHello {
        nested: ValidNestedFidl,
    }

    assert_eq!(
        ValidHello::try_from(FidlHello { nested: Some(NestedFidl { required: Some(10) }) })
            .expect("validation"),
        ValidHello { nested: ValidNestedFidl { required: 10 } },
    );
}

#[test]
fn nested_invalid_field_rejected() {
    struct NestedFidl {
        required: Option<usize>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(NestedFidl)]
    struct ValidNestedFidl {
        required: usize,
    }

    struct FidlHello {
        nested: Option<NestedFidl>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(FidlHello)]
    struct ValidHello {
        nested: ValidNestedFidl,
    }

    match ValidHello::try_from(FidlHello { nested: Some(NestedFidl { required: None }) }) {
        Err(FidlHelloValidationError::InvalidField(_)) => {}
        r => panic!("Wanted invalid field error for invalid nested field; got {:?}", r),
    }
}

#[test]
fn back_into_original_nested() {
    #[derive(Debug, PartialEq)]
    struct NestedFidl {
        required: Option<usize>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(NestedFidl)]
    struct ValidNestedFidl {
        required: usize,
    }

    #[derive(Debug, PartialEq)]
    struct FidlHello {
        nested: Option<NestedFidl>,
    }

    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(FidlHello)]
    struct ValidHello {
        nested: ValidNestedFidl,
    }

    assert_eq!(
        FidlHello::from(ValidHello { nested: ValidNestedFidl { required: 10 } }),
        FidlHello { nested: Some(NestedFidl { required: Some(10) }) }
    );
}

mod nested {
    #[derive(Debug, PartialEq)]
    pub(crate) struct FidlHello {
        pub required: Option<usize>,
    }
}

#[test]
fn works_with_nested_typenames() {
    #[derive(ValidFidlTable, Debug, PartialEq)]
    #[fidl_table_src(nested::FidlHello)]
    struct ValidHello {
        required: usize,
    }

    match ValidHello::try_from(nested::FidlHello { required: Some(7) }) {
        Ok(valid_hello) => assert_eq!(ValidHello { required: 7 }, valid_hello),
        Err(e) => panic!("Did not expect to fail to build ValidHello: got {:?}", e),
    };
}
