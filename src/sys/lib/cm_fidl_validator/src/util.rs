// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::error::*, std::collections::HashMap};

const MAX_PATH_LENGTH: usize = 1024;
const MAX_NAME_LENGTH: usize = 100;
const MAX_URL_LENGTH: usize = 4096;

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
            errors.push(Error::field_too_long(decl_type, keyword))
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

pub(crate) fn check_name(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(MAX_NAME_LENGTH, prop, decl_type, keyword, errors);
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

// TODO: This should probably be checking with the `url` crate
pub(crate) fn check_url(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(MAX_URL_LENGTH, prop, decl_type, keyword, errors);
    if let Some(url) = prop {
        let mut chars_iter = url.chars();
        let mut first_char = true;
        while let Some(c) = chars_iter.next() {
            match c {
                '0'..='9' | 'a'..='z' | '+' | '-' | '.' => first_char = false,
                ':' => {
                    if first_char {
                        // There must be at least one character in the schema
                        errors.push(Error::invalid_field(decl_type, keyword));
                        return false;
                    }
                    // Once a `:` character is found, it must be followed by two `/` characters and
                    // then at least one more character. Note that these sequential calls to
                    // `.next()` without checking the result won't panic because `Chars` implements
                    // `FusedIterator`.
                    match (chars_iter.next(), chars_iter.next(), chars_iter.next()) {
                        (Some('/'), Some('/'), Some(_)) => return start_err_len == errors.len(),
                        _ => {
                            errors.push(Error::invalid_field(decl_type, keyword));
                            return false;
                        }
                    }
                }
                // If the first character is # then it's a relative URL.
                // It must have at least one more character.
                '#' => {
                    if first_char && chars_iter.next().is_some() {
                        return start_err_len == errors.len();
                    }
                    errors.push(Error::invalid_field(decl_type, keyword));
                    return false;
                }
                _ => {
                    errors.push(Error::invalid_field(decl_type, keyword));
                    return false;
                }
            }
        }
        // If we've reached here then the string terminated unexpectedly
        errors.push(Error::invalid_field(decl_type, keyword));
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
    use {super::*, lazy_static::lazy_static, proptest::prelude::*, regex::Regex};

    const PATH_REGEX_STR: &str = r"(/[^/]+)+";
    const NAME_REGEX_STR: &str = r"[0-9a-zA-Z_][0-9a-zA-Z_\-\.]*";
    const URL_REGEX_STR: &str = r"([0-9a-z\+\-\.]+://.+|#.+)";

    lazy_static! {
        static ref PATH_REGEX: Regex =
            Regex::new(&("^".to_string() + PATH_REGEX_STR + "$")).unwrap();
        static ref NAME_REGEX: Regex =
            Regex::new(&("^".to_string() + NAME_REGEX_STR + "$")).unwrap();
        static ref URL_REGEX: Regex = Regex::new(&("^".to_string() + URL_REGEX_STR + "$")).unwrap();
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
        #[test]
        fn check_url_fails_invalid_input(s in ".*") {
            if !URL_REGEX.is_match(&s) {
                let mut errors = vec![];
                prop_assert!(!check_url(Some(&s), "", "", &mut errors));
                prop_assert!(!errors.is_empty());
            }
        }


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
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
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
            result = Err(ErrorList::new(vec![Error::field_too_long("FooDecl", "foo")])),
        },

        // name
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
            input = &format!("{}", "a".repeat(101)),
            result = Err(ErrorList::new(vec![Error::field_too_long("FooDecl", "foo")])),
        },

        // url
        test_identifier_url_valid => {
            check_fn = check_url,
            input = "my+awesome-scheme.2://abc123!@#$%.com",
            result = Ok(()),
        },
        test_identifier_url_invalid => {
            check_fn = check_url,
            input = "fuchsia-pkg://",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_url_too_long => {
            check_fn = check_url,
            input = &format!("fuchsia-pkg://{}", "a".repeat(4083)),
            result = Err(ErrorList::new(vec![Error::field_too_long("FooDecl", "foo")])),
        },
    }
}
