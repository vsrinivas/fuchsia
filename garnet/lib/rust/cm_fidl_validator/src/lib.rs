// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_sys2 as fsys,
    lazy_static::lazy_static,
    regex::Regex,
    std::collections::{HashMap, HashSet},
    std::error,
    std::fmt,
};

lazy_static! {
    static ref PATH: Identifier = Identifier::new(r"^(/[^/]+)+$", 1024);
    static ref NAME: Identifier = Identifier::new(r"^[0-9a-z_\-\.]+$", 100);
    static ref URI: Identifier = Identifier::new(r"^[0-9a-z\+\-\.]+://.+$", 4096);
}

/// Enum type that can represent any error encountered during validation.
#[derive(Debug)]
pub enum Error {
    MissingField(String, String),
    EmptyField(String, String),
    DuplicateField(String, String, String),
    InvalidField(String, String),
    FieldTooLong(String, String),
    OfferTargetEqualsSource(String),
    InvalidChild(String, String),
}

impl Error {
    pub fn missing_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::MissingField(decl_type.into(), keyword.into())
    }

    pub fn empty_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::EmptyField(decl_type.into(), keyword.into())
    }

    pub fn duplicate_field(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        value: impl Into<String>,
    ) -> Self {
        Error::DuplicateField(decl_type.into(), keyword.into(), value.into())
    }

    pub fn invalid_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::InvalidField(decl_type.into(), keyword.into())
    }

    pub fn field_too_long(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::FieldTooLong(decl_type.into(), keyword.into())
    }

    pub fn offer_target_equals_source(decl_type: impl Into<String>) -> Self {
        Error::OfferTargetEqualsSource(decl_type.into())
    }

    pub fn invalid_child(decl_type: impl Into<String>, child: impl Into<String>) -> Self {
        Error::InvalidChild(decl_type.into(), child.into())
    }
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match &self {
            Error::MissingField(d, k) => write!(f, "{} missing {}", d, k),
            Error::EmptyField(d, k) => write!(f, "{} has empty {}", d, k),
            Error::DuplicateField(d, k, v) => write!(f, "\"{}\" is a duplicate {} {}", v, d, k),
            Error::InvalidField(d, k) => write!(f, "{} has invalid {}", d, k),
            Error::FieldTooLong(d, k) => write!(f, "{}'s {} is too long", d, k),
            Error::OfferTargetEqualsSource(d) => {
                write!(f, "OfferTarget \"{}\" is same as source", d)
            }
            Error::InvalidChild(d, c) => {
                write!(f, "\"{}\" is referenced in {} but it does not appear in children", c, d)
            }
        }
    }
}

/// Represents a list of errors encountered during validation.
#[derive(Debug)]
pub struct ErrorList {
    errs: Vec<Error>,
}

impl ErrorList {
    fn new(errs: Vec<Error>) -> ErrorList {
        ErrorList { errs }
    }
}

impl error::Error for ErrorList {}

impl fmt::Display for ErrorList {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let strs: Vec<String> = self.errs.iter().map(|e| format!("{}", e)).collect();
        write!(f, "{}", strs.join(", "))
    }
}

/// Validates a ComponentDecl.
/// The ComponentDecl may ultimately originate from a CM file, or be directly constructed by the
/// caller. Either way, a ComponentDecl should always be validated before it's used. Examples
/// of what is validated (which may evolve in the future):
/// - That all semantically required fields are present
/// - That a child_name referenced in a source actually exists in the list of children
/// - That there are no duplicate target ptahs
pub fn validate(decl: &fsys::ComponentDecl) -> Result<(), ErrorList> {
    let ctx = ValidationContext { decl, all_children: HashSet::new(), errors: vec![] };
    ctx.validate().map_err(|errs| ErrorList::new(errs))
}

struct ValidationContext<'a> {
    decl: &'a fsys::ComponentDecl,
    all_children: HashSet<&'a str>,
    errors: Vec<Error>,
}

type PathMap<'a> = HashMap<String, HashSet<&'a str>>;

impl<'a> ValidationContext<'a> {
    fn validate(mut self) -> Result<(), Vec<Error>> {
        // Validate "children" and build the set of all children.
        if let Some(children) = self.decl.children.as_ref() {
            for child in children.iter() {
                self.validate_child_decl(&child);
            }
        }

        // Validate "uses".
        if let Some(uses) = self.decl.uses.as_ref() {
            for use_ in uses.iter() {
                self.validate_use_decl(&use_);
            }
        }

        // Validate "exposes".
        if let Some(exposes) = self.decl.exposes.as_ref() {
            let mut target_paths = HashSet::new();
            for expose in exposes.iter() {
                self.validate_expose_decl(&expose, &mut target_paths);
            }
        }

        // Validate "offers".
        if let Some(offers) = self.decl.offers.as_ref() {
            let mut target_paths = HashMap::new();
            for offer in offers.iter() {
                self.validate_offer_decl(&offer, &mut target_paths);
            }
        }

        if self.errors.is_empty() {
            Ok(())
        } else {
            Err(self.errors)
        }
    }

    fn validate_use_decl(&mut self, use_: &fsys::UseDecl) {
        match use_ {
            fsys::UseDecl::Service(u) => {
                self.validate_use_fields(
                    "UseServiceDecl",
                    u.source_path.as_ref(),
                    u.target_path.as_ref(),
                );
            }
            fsys::UseDecl::Directory(u) => {
                self.validate_use_fields(
                    "UseDirectoryDecl",
                    u.source_path.as_ref(),
                    u.target_path.as_ref(),
                );
            }
            fsys::UseDecl::__UnknownVariant { .. } => {
                self.errors.push(Error::invalid_field("ComponentDecl", "use"));
            }
        }
    }

    fn validate_use_fields(
        &mut self,
        decl: &str,
        source_path: Option<&String>,
        target_path: Option<&String>,
    ) {
        PATH.check(source_path, decl, "source_path", &mut self.errors);
        PATH.check(target_path, decl, "target_path", &mut self.errors);
    }

    fn validate_child_decl(&mut self, child: &'a fsys::ChildDecl) {
        let name = child.name.as_ref();
        if NAME.check(name, "ChildDecl", "name", &mut self.errors) {
            let name: &str = name.unwrap();
            if !self.all_children.insert(name) {
                self.errors.push(Error::duplicate_field("ChildDecl", "name", name));
            }
        }
        URI.check(child.uri.as_ref(), "ChildDecl", "uri", &mut self.errors);
        if child.startup.is_none() {
            self.errors.push(Error::missing_field("ChildDecl", "startup"));
        }
    }

    fn validate_source_child(&mut self, child: &fsys::ChildId, decl_type: &str) {
        if NAME.check(child.name.as_ref(), decl_type, "source.child.name", &mut self.errors) {
            if let Some(child_name) = &child.name {
                if !self.all_children.contains(child_name as &str) {
                    self.errors.push(Error::invalid_child(
                        format!("{} source", decl_type),
                        child_name as &str,
                    ));
                }
            } else {
                self.errors.push(Error::missing_field(decl_type, "source.child.name"));
            }
        }
    }

    fn validate_expose_decl(
        &mut self,
        expose: &'a fsys::ExposeDecl,
        prev_target_paths: &mut HashSet<&'a str>,
    ) {
        match expose {
            fsys::ExposeDecl::Service(e) => {
                self.validate_expose_fields(
                    "ExposeServiceDecl",
                    e.source.as_ref(),
                    e.source_path.as_ref(),
                    e.target_path.as_ref(),
                    prev_target_paths,
                );
            }
            fsys::ExposeDecl::Directory(e) => {
                self.validate_expose_fields(
                    "ExposeDirectoryDecl",
                    e.source.as_ref(),
                    e.source_path.as_ref(),
                    e.target_path.as_ref(),
                    prev_target_paths,
                );
            }
            fsys::ExposeDecl::__UnknownVariant { .. } => {
                self.errors.push(Error::invalid_field("ComponentDecl", "expose"));
            }
        }
    }

    fn validate_expose_fields(
        &mut self,
        decl: &str,
        source: Option<&fsys::ExposeSource>,
        source_path: Option<&String>,
        target_path: Option<&'a String>,
        prev_target_paths: &mut HashSet<&'a str>,
    ) {
        match source {
            Some(r) => match r {
                fsys::ExposeSource::Myself(_) => {}
                fsys::ExposeSource::Child(child) => {
                    self.validate_source_child(child, decl);
                }
                fsys::ExposeSource::__UnknownVariant { .. } => {
                    self.errors.push(Error::invalid_field(decl, "source"));
                }
            },
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
            }
        }
        PATH.check(source_path, decl, "source_path", &mut self.errors);
        if PATH.check(target_path, decl, "target_path", &mut self.errors) {
            let target_path: &str = target_path.unwrap();
            if !prev_target_paths.insert(target_path) {
                self.errors.push(Error::duplicate_field(decl, "target_path", target_path));
            }
        }
    }

    fn validate_offer_decl(
        &mut self,
        offer: &'a fsys::OfferDecl,
        prev_target_paths: &mut PathMap<'a>,
    ) {
        match offer {
            fsys::OfferDecl::Service(o) => {
                self.validate_offer_fields(
                    "OfferServiceDecl",
                    o.source.as_ref(),
                    o.source_path.as_ref(),
                    o.targets.as_ref(),
                    prev_target_paths,
                );
            }
            fsys::OfferDecl::Directory(o) => {
                self.validate_offer_fields(
                    "OfferDirectoryDecl",
                    o.source.as_ref(),
                    o.source_path.as_ref(),
                    o.targets.as_ref(),
                    prev_target_paths,
                );
            }
            fsys::OfferDecl::__UnknownVariant { .. } => {
                self.errors.push(Error::invalid_field("ComponentDecl", "offer"));
            }
        }
    }

    fn validate_offer_fields(
        &mut self,
        decl: &str,
        source: Option<&fsys::OfferSource>,
        source_path: Option<&String>,
        targets: Option<&'a Vec<fsys::OfferTarget>>,
        prev_target_paths: &mut PathMap<'a>,
    ) {
        match source {
            Some(r) => match r {
                fsys::OfferSource::Realm(_) => {}
                fsys::OfferSource::Myself(_) => {}
                fsys::OfferSource::Child(child) => {
                    self.validate_source_child(child, decl);
                }
                fsys::OfferSource::__UnknownVariant { .. } => {
                    self.errors.push(Error::invalid_field(decl, "source"));
                }
            },
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
            }
        }
        PATH.check(source_path, decl, "source_path", &mut self.errors);
        if let Some(targets) = targets {
            self.validate_targets(decl, source, targets, prev_target_paths);
        } else {
            self.errors.push(Error::missing_field(decl, "targets"));
        }
    }

    fn validate_targets(
        &mut self,
        decl: &str,
        source: Option<&fsys::OfferSource>,
        targets: &'a Vec<fsys::OfferTarget>,
        prev_target_paths: &mut PathMap<'a>,
    ) {
        if targets.is_empty() {
            self.errors.push(Error::empty_field(decl, "targets"));
        }
        for target in targets.iter() {
            let mut valid = true;
            valid &= PATH.check(
                target.target_path.as_ref(),
                "OfferTarget",
                "target_path",
                &mut self.errors,
            );
            valid &= NAME.check(
                target.child_name.as_ref(),
                "OfferTarget",
                "child_name",
                &mut self.errors,
            );
            if valid {
                let target_path: &str = target.target_path.as_ref().unwrap();
                let child_name: &str = target.child_name.as_ref().unwrap();
                if !self.all_children.contains(child_name) {
                    self.errors.push(Error::invalid_child("OfferTarget", child_name));
                }

                let paths_for_target =
                    prev_target_paths.entry(child_name.to_string()).or_insert(HashSet::new());
                if !paths_for_target.insert(target_path) {
                    self.errors.push(Error::duplicate_field(
                        decl,
                        "target_path",
                        target_path,
                    ));
                }

                if let Some(source) = source {
                    if let fsys::OfferSource::Child(source_child) = source {
                        if let Some(source_child_name) = &source_child.name {
                            if source_child_name == child_name {
                                self.errors.push(Error::offer_target_equals_source(
                                    source_child_name as &str,
                                ));
                            }
                        }
                    }
                }
            }
        }
    }
}

struct Identifier {
    re: Regex,
    max_len: usize,
}

impl Identifier {
    fn new(regex: &str, max_len: usize) -> Identifier {
        Identifier { re: Regex::new(regex).unwrap(), max_len }
    }

    fn check(
        &self,
        prop: Option<&String>,
        decl_type: &str,
        keyword: &str,
        errors: &mut Vec<Error>,
    ) -> bool {
        let mut valid = true;
        if prop.is_none() {
            errors.push(Error::missing_field(decl_type, keyword));
            valid = false;
        } else {
            if !self.re.is_match(prop.unwrap()) {
                errors.push(Error::invalid_field(decl_type, keyword));
                valid = false;
            }
            if prop.unwrap().len() > self.max_len {
                errors.push(Error::field_too_long(decl_type, keyword));
                valid = false;
            }
        }
        valid
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_sys2::{
            ChildDecl, ChildId, ComponentDecl, ExposeDecl, ExposeDirectoryDecl, ExposeServiceDecl,
            ExposeSource, OfferDecl, OfferDirectoryDecl, OfferServiceDecl, OfferSource,
            OfferTarget, RealmId, SelfId, StartupMode, UseDecl, UseDirectoryDecl, UseServiceDecl,
        },
    };

    fn validate_test(input: ComponentDecl, expected_res: Result<(), ErrorList>) {
        let res = validate(&input);
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    }

    fn identifier_test(identifier: &Identifier, input: &str, expected_res: Result<(), ErrorList>) {
        let mut errors = vec![];
        let res: Result<(), ErrorList> =
            match identifier.check(Some(&input.to_string()), "FooDecl", "foo", &mut errors) {
                true => Ok(()),
                false => Err(ErrorList::new(errors)),
            };
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
    }

    fn new_component_decl() -> ComponentDecl {
        ComponentDecl {
            program: None,
            uses: None,
            exposes: None,
            offers: None,
            facets: None,
            storage: None,
            children: None,
        }
    }

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
            format!("{}", Error::invalid_child("Decl", "child")),
            "\"child\" is referenced in Decl but it does not appear in children"
        );
    }

    macro_rules! test_validate {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    validate_test($input, $result);
                }
            )+
        }
    }

    macro_rules! test_identifier {
        (
            $(
                $test_name:ident => {
                    identifier = $identifier:expr,
                    input = $input:expr,
                    result = $result:expr,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    identifier_test($identifier, $input, $result);
                }
            )+
        }
    }

    test_identifier! {
        // path
        test_identifier_path_valid => {
            identifier = &PATH,
            input = "/foo/bar",
            result = Ok(()),
        },
        test_identifier_path_invalid_empty => {
            identifier = &PATH,
            input = "",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_path_invalid_root => {
            identifier = &PATH,
            input = "/",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_path_invalid_relative => {
            identifier = &PATH,
            input = "foo/bar",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_path_invalid_trailing => {
            identifier = &PATH,
            input = "/foo/bar/",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_path_too_long => {
            identifier = &PATH,
            input = &format!("/{}", "a".repeat(1024)),
            result = Err(ErrorList::new(vec![Error::field_too_long("FooDecl", "foo")])),
        },

        // name
        test_identifier_name_valid => {
            identifier = &NAME,
            input = "abcdefghijklmnopqrstuvwxyz0123456789_-.",
            result = Ok(()),
        },
        test_identifier_name_invalid => {
            identifier = &NAME,
            input = "^bad",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_name_too_long => {
            identifier = &NAME,
            input = &format!("{}", "a".repeat(101)),
            result = Err(ErrorList::new(vec![Error::field_too_long("FooDecl", "foo")])),
        },

        // uri
        test_identifier_uri_valid => {
            identifier = &URI,
            input = "my+awesome-scheme.2://abc123!@#$%.com",
            result = Ok(()),
        },
        test_identifier_uri_invalid => {
            identifier = &URI,
            input = "fuchsia-pkg://",
            result = Err(ErrorList::new(vec![Error::invalid_field("FooDecl", "foo")])),
        },
        test_identifier_uri_too_long => {
            identifier = &URI,
            input = &format!("fuchsia-pkg://{}", "a".repeat(4083)),
            result = Err(ErrorList::new(vec![Error::field_too_long("FooDecl", "foo")])),
        },
    }

    test_validate! {
        // uses
        test_validate_uses_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source_path: None,
                        target_path: None,
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source_path: None,
                        target_path: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("UseServiceDecl", "source_path"),
                Error::missing_field("UseServiceDecl", "target_path"),
                Error::missing_field("UseDirectoryDecl", "source_path"),
                Error::missing_field("UseDirectoryDecl", "target_path"),
            ])),
        },
        test_validate_uses_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseServiceDecl", "source_path"),
                Error::invalid_field("UseServiceDecl", "target_path"),
                Error::invalid_field("UseDirectoryDecl", "source_path"),
                Error::invalid_field("UseDirectoryDecl", "target_path"),
            ])),
        },
        test_validate_uses_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("UseServiceDecl", "source_path"),
                Error::field_too_long("UseServiceDecl", "target_path"),
                Error::field_too_long("UseDirectoryDecl", "source_path"),
                Error::field_too_long("UseDirectoryDecl", "target_path"),
            ])),
        },

        // exposes
        test_validate_exposes_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ExposeServiceDecl", "source"),
                Error::missing_field("ExposeServiceDecl", "source_path"),
                Error::missing_field("ExposeServiceDecl", "target_path"),
                Error::missing_field("ExposeDirectoryDecl", "source"),
                Error::missing_field("ExposeDirectoryDecl", "source_path"),
                Error::missing_field("ExposeDirectoryDecl", "target_path"),
            ])),
        },
        test_validate_exposes_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(ExposeSource::Child(ChildId{name: Some("^bad".to_string())})),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(ExposeSource::Child(ChildId{name: Some("^bad".to_string())})),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ExposeServiceDecl", "source.child.name"),
                Error::invalid_field("ExposeServiceDecl", "source_path"),
                Error::invalid_field("ExposeServiceDecl", "target_path"),
                Error::invalid_field("ExposeDirectoryDecl", "source.child.name"),
                Error::invalid_field("ExposeDirectoryDecl", "source_path"),
                Error::invalid_field("ExposeDirectoryDecl", "target_path"),
            ])),
        },
        test_validate_exposes_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(ExposeSource::Child(ChildId{name: Some("b".repeat(101))})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(ExposeSource::Child(ChildId{name: Some("b".repeat(101))})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ExposeServiceDecl", "source.child.name"),
                Error::field_too_long("ExposeServiceDecl", "source_path"),
                Error::field_too_long("ExposeServiceDecl", "target_path"),
                Error::field_too_long("ExposeDirectoryDecl", "source.child.name"),
                Error::field_too_long("ExposeDirectoryDecl", "source_path"),
                Error::field_too_long("ExposeDirectoryDecl", "target_path"),
            ])),
        },
        test_validate_exposes_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(ExposeSource::Child(ChildId {
                            name: Some("netstack".to_string()),
                        })),
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(ExposeSource::Child(ChildId {
                            name: Some("netstack".to_string()),
                        })),
                        source_path: Some("/data/netstack".to_string()),
                        target_path: Some("/data".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("ExposeServiceDecl source", "netstack"),
                Error::invalid_child("ExposeDirectoryDecl source", "netstack"),
            ])),
        },
        test_validate_exposes_duplicate_target => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(ExposeSource::Myself(SelfId{})),
                        source_path: Some("/svc/logger".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(ExposeSource::Myself(SelfId{})),
                        source_path: Some("/svc/logger2".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("ExposeDirectoryDecl", "target_path",
                                       "/svc/fuchsia.logger.Log"),
            ])),
        },

        // offers
        test_validate_offers_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: None,
                        source_path: None,
                        targets: None,
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: None,
                        source_path: None,
                        targets: None,
                    })
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("OfferServiceDecl", "source"),
                Error::missing_field("OfferServiceDecl", "source_path"),
                Error::missing_field("OfferServiceDecl", "targets"),
                Error::missing_field("OfferDirectoryDecl", "source"),
                Error::missing_field("OfferDirectoryDecl", "source_path"),
                Error::missing_field("OfferDirectoryDecl", "targets"),
            ])),
        },
        test_validate_offers_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(OfferSource::Child(ChildId{name: Some("a".repeat(101))})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        targets: Some(vec![
                            OfferTarget {
                                target_path: Some(format!("/{}", "b".repeat(1024))),
                                child_name: Some("b".repeat(101)),
                            },
                        ]),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(OfferSource::Child(ChildId{name: Some("a".repeat(101))})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        targets: Some(vec![
                            OfferTarget {
                                target_path: Some(format!("/{}", "b".repeat(1024))),
                                child_name: Some("b".repeat(101)),
                            },
                        ]),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("OfferServiceDecl", "source.child.name"),
                Error::field_too_long("OfferServiceDecl", "source_path"),
                Error::field_too_long("OfferTarget", "target_path"),
                Error::field_too_long("OfferTarget", "child_name"),
                Error::field_too_long("OfferDirectoryDecl", "source.child.name"),
                Error::field_too_long("OfferDirectoryDecl", "source_path"),
                Error::field_too_long("OfferTarget", "target_path"),
                Error::field_too_long("OfferTarget", "child_name"),
            ])),
        },
        test_validate_offers_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(OfferSource::Child(ChildId{name: Some("logger".to_string())})),
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        targets: Some(vec![
                            OfferTarget {
                                target_path: Some("/data/realm_assets".to_string()),
                                child_name: Some("netstack".to_string()),
                            },
                        ]),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(OfferSource::Child(ChildId{name: Some("logger".to_string())})),
                        source_path: Some("/data/assets".to_string()),
                        targets: Some(vec![
                            OfferTarget {
                                target_path: Some("/data".to_string()),
                                child_name: Some("netstack".to_string()),
                            },
                        ]),
                    }),
                ]);
                decl.children = Some(vec![
                    ChildDecl{
                        name: Some("netstack".to_string()),
                        uri: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(StartupMode::Lazy),
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("OfferServiceDecl source", "logger"),
                Error::invalid_child("OfferDirectoryDecl source", "logger"),
            ])),
        },
        test_validate_offer_target_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(OfferSource::Realm(RealmId{})),
                        source_path: Some("/svc/logger".to_string()),
                        targets: Some(vec![OfferTarget{target_path: None, child_name: None}]),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(OfferSource::Realm(RealmId{})),
                        source_path: Some("/data/assets".to_string()),
                        targets: Some(vec![OfferTarget{target_path: None, child_name: None}]),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("OfferTarget", "target_path"),
                Error::missing_field("OfferTarget", "child_name"),
                Error::missing_field("OfferTarget", "target_path"),
                Error::missing_field("OfferTarget", "child_name"),
            ])),
        },
        test_validate_offer_targets_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(OfferSource::Myself(SelfId{})),
                        source_path: Some("/svc/logger".to_string()),
                        targets: Some(vec![]),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(OfferSource::Myself(SelfId{})),
                        source_path: Some("/data/assets".to_string()),
                        targets: Some(vec![]),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::empty_field("OfferServiceDecl", "targets"),
                Error::empty_field("OfferDirectoryDecl", "targets"),
            ])),
        },
        test_validate_offer_target_equals_from => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(OfferSource::Child(ChildId{name: Some("logger".to_string())})),
                        source_path: Some("/svc/logger".to_string()),
                        targets: Some(vec![OfferTarget{
                            target_path: Some("/svc/logger".to_string()),
                            child_name: Some("logger".to_string()),
                        }]),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(OfferSource::Child(ChildId{name: Some("logger".to_string())})),
                        source_path: Some("/data/assets".to_string()),
                        targets: Some(vec![OfferTarget{
                            target_path: Some("/data".to_string()),
                            child_name: Some("logger".to_string()),
                        }]),
                    }),
                ]);
                decl.children = Some(vec![ChildDecl{
                    name: Some("logger".to_string()),
                    uri: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(StartupMode::Lazy),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::offer_target_equals_source("logger"),
                Error::offer_target_equals_source("logger"),
            ])),
        },
        test_validate_offer_target_duplicate_path => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(OfferSource::Myself(SelfId{})),
                        source_path: Some("/svc/logger".to_string()),
                        targets: Some(vec![
                            OfferTarget {
                                target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                                child_name: Some("netstack".to_string()),
                            },
                            OfferTarget {
                                target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                                child_name: Some("netstack".to_string()),
                            },
                        ]),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(OfferSource::Myself(SelfId{})),
                        source_path: Some("/data/assets".to_string()),
                        targets: Some(vec![
                            OfferTarget{
                                target_path: Some("/data".to_string()),
                                child_name: Some("netstack".to_string()),
                            },
                            OfferTarget{
                                target_path: Some("/data".to_string()),
                                child_name: Some("netstack".to_string()),
                            },
                        ]),
                    }),
                ]);
                decl.children = Some(vec![
                    ChildDecl{
                        name: Some("netstack".to_string()),
                        uri: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(StartupMode::Eager),
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("OfferServiceDecl", "target_path", "/svc/fuchsia.logger.Log"),
                Error::duplicate_field("OfferDirectoryDecl", "target_path", "/data"),
            ])),
        },
        test_validate_offer_target_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(OfferSource::Myself(SelfId{})),
                        source_path: Some("/svc/logger".to_string()),
                        targets: Some(vec![
                            OfferTarget{
                                target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                                child_name: Some("netstack".to_string()),
                            },
                        ]),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(OfferSource::Myself(SelfId{})),
                        source_path: Some("/data/assets".to_string()),
                        targets: Some(vec![
                            OfferTarget{
                                target_path: Some("/data".to_string()),
                                child_name: Some("netstack".to_string()),
                            },
                        ]),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("OfferTarget", "netstack"),
                Error::invalid_child("OfferTarget", "netstack"),
            ])),
        },

        // children
        test_validate_children_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: None,
                    uri: None,
                    startup: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ChildDecl", "name"),
                Error::missing_field("ChildDecl", "uri"),
                Error::missing_field("ChildDecl", "startup"),
            ])),
        },
        test_validate_children_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: Some("^bad".to_string()),
                    uri: Some("bad-scheme&://blah".to_string()),
                    startup: Some(StartupMode::Lazy),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("ChildDecl", "name"),
                Error::invalid_field("ChildDecl", "uri"),
            ])),
        },
        test_validate_children_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: Some("a".repeat(1025)),
                    uri: Some(format!("fuchsia-pkg://{}", "a".repeat(4083))),
                    startup: Some(StartupMode::Lazy),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ChildDecl", "name"),
                Error::field_too_long("ChildDecl", "uri"),
            ])),
        },
    }
}
