// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::error::*, fidl_fuchsia_component_decl as fdecl, std::collections::HashMap};

const MAX_PATH_LENGTH: usize = 1024;
const MAX_URL_LENGTH: usize = 4096;
pub const MAX_NAME_LENGTH: usize = 100;
pub const MAX_DYNAMIC_NAME_LENGTH: usize = 1024;

#[derive(Clone, Copy, PartialEq)]
pub(crate) enum AllowableIds {
    One,
    Many,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum CollectionSource {
    Allow,
    Deny,
}

#[derive(Debug, PartialEq, Eq, Hash)]
pub(crate) enum TargetId<'a> {
    Component(&'a str),
    Collection(&'a str),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum OfferType {
    Static,
    Dynamic,
}

pub(crate) type IdMap<'a> = HashMap<TargetId<'a>, HashMap<&'a str, AllowableIds>>;

pub(crate) fn check_presence_and_length(
    max_len: usize,
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) {
    match prop {
        Some(prop) if prop.len() == 0 => errors.push(Error::empty_field(decl_type, keyword)),
        Some(prop) if prop.len() > max_len => {
            errors.push(Error::field_too_long_with_max(decl_type, keyword, max_len))
        }
        Some(_) => (),
        None => errors.push(Error::missing_field(decl_type, keyword)),
    }
}

pub(crate) fn check_path(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(MAX_PATH_LENGTH, prop, decl_type, keyword, errors);
    if let Some(path) = prop {
        // Paths must be more than 1 character long
        if path.len() < 2 {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Paths must start with `/`
        if !path.starts_with('/') {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Paths cannot have two `/`s in a row
        if path.contains("//") {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Paths cannot end with `/`
        if path.ends_with('/') {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
    }
    start_err_len == errors.len()
}

pub(crate) fn check_relative_path(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(MAX_PATH_LENGTH, prop, decl_type, keyword, errors);
    if let Some(path) = prop {
        // Relative paths must be nonempty
        if path.is_empty() {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Relative paths cannot start with `/`
        if path.starts_with('/') {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Relative paths cannot have two `/`s in a row
        if path.contains("//") {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
        // Relative paths cannot end with `/`
        if path.ends_with('/') {
            errors.push(Error::invalid_field(decl_type, keyword));
            return false;
        }
    }
    start_err_len == errors.len()
}

pub(crate) fn check_dynamic_name(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    check_name_impl(prop, decl_type, keyword, MAX_DYNAMIC_NAME_LENGTH, errors)
}

pub(crate) fn check_name(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    check_name_impl(prop, decl_type, keyword, MAX_NAME_LENGTH, errors)
}

fn check_name_impl(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    max_len: usize,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(max_len, prop, decl_type, keyword, errors);
    let mut invalid_field = false;
    if let Some(name) = prop {
        let mut char_iter = name.chars();
        if let Some(first_char) = char_iter.next() {
            if !first_char.is_ascii_alphanumeric() && first_char != '_' {
                invalid_field = true;
            }
        }
        for c in char_iter {
            if c.is_ascii_alphanumeric() || c == '_' || c == '-' || c == '.' {
                // Ok
            } else {
                invalid_field = true;
            }
        }
    }
    if invalid_field {
        errors.push(Error::invalid_field(decl_type, keyword));
    }
    start_err_len == errors.len()
}

pub(crate) fn check_use_availability(
    decl_type: &str,
    availability: Option<&fdecl::Availability>,
    errors: &mut Vec<Error>,
) {
    match availability {
        Some(fdecl::Availability::Required)
        | Some(fdecl::Availability::Optional)
        | Some(fdecl::Availability::Transitional) => {}
        Some(fdecl::Availability::SameAsTarget) => {
            errors.push(Error::invalid_field(decl_type, "availability"))
        }
        // TODO(dgonyeo): we need to soft migrate the requirement for this field to be set
        //None => errors.push(Error::missing_field(decl_type, "availability")),
        None => (),
    }
}

pub(crate) fn check_offer_availability(
    decl: &str,
    availability: Option<&fdecl::Availability>,
    source: Option<&fdecl::Ref>,
    source_name: Option<&String>,
    errors: &mut Vec<Error>,
) {
    match (source, availability) {
        // The availability must be optional or transitional when the source is void.
        (Some(fdecl::Ref::VoidType(_)), Some(fdecl::Availability::Optional))
        | (Some(fdecl::Ref::VoidType(_)), Some(fdecl::Availability::Transitional)) => (),
        (
            Some(fdecl::Ref::VoidType(_)),
            Some(fdecl::Availability::Required | fdecl::Availability::SameAsTarget),
        ) => errors.push(Error::availability_must_be_optional(decl, "availability", source_name)),
        // All other cases are valid
        _ => (),
    }
}

pub(crate) fn check_url(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(MAX_URL_LENGTH, prop, decl_type, keyword, errors);
    if start_err_len == errors.len() {
        if let Some(url_str) = prop {
            if let Err(err) = cm_types::Url::validate(url_str) {
                let message = match &err {
                    cm_types::ParseError::InvalidComponentUrl { details } => details.to_owned(),
                    _ => err.to_string(),
                };
                errors.push(Error::invalid_url(
                    decl_type,
                    keyword,
                    &format!(r#""{url_str}": {message}"#),
                ));
            }
        }
    }
    start_err_len == errors.len()
}

pub(crate) fn check_url_scheme(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    if let Some(scheme) = prop {
        if let Err(err) = cm_types::UrlScheme::validate(scheme) {
            errors.push(match err {
                cm_types::ParseError::InvalidLength => {
                    if scheme.is_empty() {
                        Error::empty_field(decl_type, keyword)
                    } else {
                        Error::field_too_long(decl_type, keyword)
                    }
                }
                cm_types::ParseError::InvalidValue => Error::invalid_field(decl_type, keyword),
                e => {
                    panic!("unexpected parse error: {:?}", e);
                }
            });
            return false;
        }
    } else {
        errors.push(Error::missing_field(decl_type, keyword));
        return false;
    }
    true
}

#[cfg(test)]
mod tests {
    use {super::*, lazy_static::lazy_static, proptest::prelude::*, regex::Regex, url::Url};

    const PATH_REGEX_STR: &str = r"(/[^/]+)+";
    const NAME_REGEX_STR: &str = r"[0-9a-zA-Z_][0-9a-zA-Z_\-\.]*";
    const URL_REGEX_STR: &str = r"((([a-z][0-9a-z\+\-\.]*://[0-9a-z\+\-\._!$&,;]*/)?[0-9a-z\+\-\._/=!@$&,;]+)?#[0-9a-z\+\-\._/?=!@$&,;:]+)";

    lazy_static! {
        static ref PATH_REGEX: Regex =
            Regex::new(&("^".to_string() + PATH_REGEX_STR + "$")).unwrap();
        static ref NAME_REGEX: Regex =
            Regex::new(&("^".to_string() + NAME_REGEX_STR + "$")).unwrap();
        static ref A_BASE_URL: Url = Url::parse("relative:///").unwrap();
    }

    proptest! {
        #[test]
        fn check_path_matches_regex(s in PATH_REGEX_STR) {
            if s.len() < MAX_PATH_LENGTH {
                let mut errors = vec![];
                prop_assert!(check_path(Some(&s), "", "", &mut errors));
                prop_assert!(errors.is_empty());
            }
        }
        #[test]
        fn check_name_matches_regex(s in NAME_REGEX_STR) {
            if s.len() < MAX_NAME_LENGTH {
                let mut errors = vec![];
                prop_assert!(check_name(Some(&s), "", "", &mut errors));
                prop_assert!(errors.is_empty());
            }
        }
        #[test]
        fn check_url_matches_regex(s in URL_REGEX_STR) {
            if s.len() < MAX_URL_LENGTH {
                let mut errors = vec![];
                prop_assert!(check_url(Some(&s), "", "", &mut errors));
                prop_assert!(errors.is_empty());
            }
        }
        #[test]
        fn check_path_fails_invalid_input(s in ".*") {
            if !PATH_REGEX.is_match(&s) {
                let mut errors = vec![];
                prop_assert!(!check_path(Some(&s), "", "", &mut errors));
                prop_assert!(!errors.is_empty());
            }
        }
        #[test]
        fn check_name_fails_invalid_input(s in ".*") {
            if !NAME_REGEX.is_match(&s) {
                let mut errors = vec![];
                prop_assert!(!check_name(Some(&s), "", "", &mut errors));
                prop_assert!(!errors.is_empty());
            }
        }
        // NOTE: The Url crate's parser is used to validate legal URLs. Testing
        // random strings against component URL validation is redundant, so
        // a `check_url_fails_invalid_input` is not necessary (and would be
        // non-trivial to do using just a regular expression).


    }

    fn check_test<F>(check_fn: F, input: &str, expected_res: Result<(), ErrorList>)
    where
        F: FnOnce(Option<&String>, &str, &str, &mut Vec<Error>) -> bool,
    {
        let mut errors = vec![];
        let res: Result<(), ErrorList> =
            match check_fn(Some(&input.to_string()), "FooDecl", "foo", &mut errors) {
                true => Ok(()),
                false => Err(ErrorList::new(errors)),
            };
        assert_eq!(
            format!("{:?}", res),
            format!("{:?}", expected_res),
            "Unexpected result for input: '{}'\n{}",
            input,
            {
                match Url::parse(input).or_else(|err| {
                    if err == url::ParseError::RelativeUrlWithoutBase {
                        A_BASE_URL.join(input)
                    } else {
                        Err(err)
                    }
                }) {
                    Ok(url) => format!(
                        "scheme={}, host={:?}, path={}, fragment={:?}",
                        url.scheme(),
                        url.host_str(),
                        url.path(),
                        url.fragment()
                    ),
                    Err(_) => "".to_string(),
                }
            }
        );
    }

    macro_rules! test_string_checks {
        (
            $(
                $test_name:ident => {
                    check_fn = $check_fn:expr,
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    check_test($check_fn, $input, $result);
                }
            )+
        }
    }

    test_string_checks! {
        // path
        test_identifier_path_valid => {
            check_fn = check_path,
            input = "/foo/bar",
            result = Ok(()),
        },
        test_identifier_path_invalid_empty => {
            check_fn = check_path,
            input = "",
            result = Err(ErrorList::new(vec![
                Error::empty_field("FooDecl", "foo"),
                Error::invalid_field("FooDecl", "foo"),
            ])),
        },
        test_identifier_path_invalid_root => {
            check_fn = check_path,
            input = "/",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_path_invalid_relative => {
            check_fn = check_path,
            input = "foo/bar",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_path_invalid_trailing => {
            check_fn = check_path,
            input = "/foo/bar/",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_path_too_long => {
            check_fn = check_path,
            input = &format!("/{}", "a".repeat(1024)),
            result = Err(ErrorList::new(vec![Error::field_too_long_with_max("FooDecl", "foo", /*max=*/1024usize)])),
        },

        // name
        test_identifier_dynamic_name_valid => {
            check_fn = check_dynamic_name,
            input = &format!("{}", "a".repeat(MAX_DYNAMIC_NAME_LENGTH)),
            result = Ok(()),
        },
        test_identifier_name_valid => {
            check_fn = check_name,
            input = "abcdefghijklmnopqrstuvwxyz0123456789_-.",
            result = Ok(()),
        },
        test_identifier_name_invalid => {
            check_fn = check_name,
            input = "^bad",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_name_too_long => {
            check_fn = check_name,
            input = &format!("{}", "a".repeat(MAX_NAME_LENGTH + 1)),
            result = Err(ErrorList::new(vec![Error::field_too_long_with_max("FooDecl", "foo", MAX_NAME_LENGTH)])),
        },
        test_identifier_dynamic_name_too_long => {
            check_fn = check_dynamic_name,
            input = &format!("{}", "a".repeat(MAX_DYNAMIC_NAME_LENGTH + 1)),
            result = Err(ErrorList::new(vec![Error::field_too_long_with_max("FooDecl", "foo", MAX_DYNAMIC_NAME_LENGTH)])),
        },

        // url
        test_identifier_url_valid => {
            check_fn = check_url,
            input = "my+awesome-scheme.2://abc123!@#$%.com",
            result = Ok(()),
        },
        test_host_path_url_valid => {
            check_fn = check_url,
            input = "some-scheme://host/path/segments",
            result = Ok(()),
        },
        test_host_path_resource_url_valid => {
            check_fn = check_url,
            input = "some-scheme://host/path/segments#meta/comp.cm",
            result = Ok(()),
        },
        test_nohost_path_resource_url_valid => {
            check_fn = check_url,
            input = "some-scheme:///path/segments#meta/comp.cm",
            result = Ok(()),
        },
        test_relative_path_resource_url_valid => {
            check_fn = check_url,
            input = "path/segments#meta/comp.cm",
            result = Ok(()),
        },
        test_relative_resource_url_valid => {
            check_fn = check_url,
            input = "path/segments#meta/comp.cm",
            result = Ok(()),
        },
        test_relative_path_url_without_resource_invalid => {
            check_fn = check_url,
            input = "path/segments",
            result = Err(ErrorList::new(vec![Error::invalid_url("FooDecl", "foo", "\"path/segments\": Relative URL has no resource fragment.")])),
        },
        test_identifier_url_invalid => {
            check_fn = check_url,
            input = "fuchsia-pkg://",
            result = Err(ErrorList::new(vec![Error::invalid_url("FooDecl", "foo","\"fuchsia-pkg://\": URL is missing either `host`, `path`, and/or `resource`.")])),
        },
        test_url_bad_scheme => {
            check_fn = check_url,
            input = "bad-scheme&://blah",
            result = Err(ErrorList::new(vec![Error::invalid_url("FooDecl", "foo", "\"bad-scheme&://blah\": Invalid scheme")])),
        },
        test_identifier_url_too_long => {
            check_fn = check_url,
            input = &format!("fuchsia-pkg://{}", "a".repeat(4083)),
            result = Err(ErrorList::new(vec![Error::field_too_long_with_max("FooDecl", "foo", 4096)])),
        },
    }
}
