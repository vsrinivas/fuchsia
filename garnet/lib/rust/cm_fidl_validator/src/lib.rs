// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_sys2 as fsys,
    std::collections::{HashMap, HashSet},
    std::error,
    std::fmt,
};

const MAX_PATH_LENGTH: usize = 1024;
const MAX_NAME_LENGTH: usize = 100;
const MAX_URL_LENGTH: usize = 4096;

/// Enum type that can represent any error encountered durlng validation.
#[derive(Debug)]
pub enum Error {
    MissingField(String, String),
    EmptyField(String, String),
    ExtraneousField(String, String),
    DuplicateField(String, String, String),
    InvalidField(String, String),
    InvalidCharacterInField(String, String, char),
    FieldTooLong(String, String),
    OfferTargetEqualsSource(String, String),
    InvalidChild(String, String, String),
    InvalidCollection(String, String, String),
    InvalidStorage(String, String, String),
    MultipleRunnersSpecified(String),
}

impl Error {
    pub fn missing_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::MissingField(decl_type.into(), keyword.into())
    }

    pub fn empty_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::EmptyField(decl_type.into(), keyword.into())
    }

    pub fn extraneous_field(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::ExtraneousField(decl_type.into(), keyword.into())
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

    pub fn invalid_character_in_field(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        character: char,
    ) -> Self {
        Error::InvalidCharacterInField(decl_type.into(), keyword.into(), character)
    }

    pub fn field_too_long(decl_type: impl Into<String>, keyword: impl Into<String>) -> Self {
        Error::FieldTooLong(decl_type.into(), keyword.into())
    }

    pub fn offer_target_equals_source(decl: impl Into<String>, target: impl Into<String>) -> Self {
        Error::OfferTargetEqualsSource(decl.into(), target.into())
    }

    pub fn invalid_child(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        child: impl Into<String>,
    ) -> Self {
        Error::InvalidChild(decl_type.into(), keyword.into(), child.into())
    }

    pub fn invalid_collection(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        collection: impl Into<String>,
    ) -> Self {
        Error::InvalidCollection(decl_type.into(), keyword.into(), collection.into())
    }

    pub fn invalid_storage(
        decl_type: impl Into<String>,
        keyword: impl Into<String>,
        storage: impl Into<String>,
    ) -> Self {
        Error::InvalidStorage(decl_type.into(), keyword.into(), storage.into())
    }

    pub fn multiple_runners_specified(decl_type: impl Into<String>) -> Self {
        Error::MultipleRunnersSpecified(decl_type.into())
    }
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self {
            Error::MissingField(d, k) => write!(f, "{} missing {}", d, k),
            Error::EmptyField(d, k) => write!(f, "{} has empty {}", d, k),
            Error::ExtraneousField(d, k) => write!(f, "{} has extraneous {}", d, k),
            Error::DuplicateField(d, k, v) => write!(f, "\"{}\" is a duplicate {} {}", v, d, k),
            Error::InvalidField(d, k) => write!(f, "{} has invalid {}", d, k),
            Error::InvalidCharacterInField(d, k, c) => {
                write!(f, "{} has invalid {}, unexpected character '{}'", d, k, c)
            }
            Error::FieldTooLong(d, k) => write!(f, "{}'s {} is too long", d, k),
            Error::OfferTargetEqualsSource(d, t) => {
                write!(f, "\"{}\" target \"{}\" is same as source", d, t)
            }
            Error::InvalidChild(d, k, c) => write!(
                f,
                "\"{}\" is referenced in {}.{} but it does not appear in children",
                c, d, k
            ),
            Error::InvalidCollection(d, k, c) => write!(
                f,
                "\"{}\" is referenced in {}.{} but it does not appear in collections",
                c, d, k
            ),
            Error::InvalidStorage(d, k, s) => write!(
                f,
                "\"{}\" is referenced in {}.{} but it does not appear in storage",
                s, d, k
            ),
            Error::MultipleRunnersSpecified(d) => write!(f, "{} specifies multiple runners", d),
        }
    }
}

/// Represents a list of errors encountered durlng validation.
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
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let strs: Vec<String> = self.errs.iter().map(|e| format!("{}", e)).collect();
        write!(f, "{}", strs.join(", "))
    }
}

/// Validates a ComponentDecl.
///
/// The ComponentDecl may ultimately originate from a CM file, or be directly constructed by the
/// caller. Either way, a ComponentDecl should always be validated before it's used. Examples
/// of what is validated (which may evolve in the future):
///
/// - That all semantically required fields are present
/// - That a child_name referenced in a source actually exists in the list of children
/// - That there are no duplicate target paths.
/// - That a cap is not offered back to the child that exposed it.
///
/// All checks are local to this ComponentDecl.
pub fn validate(decl: &fsys::ComponentDecl) -> Result<(), ErrorList> {
    let ctx = ValidationContext {
        decl,
        all_children: HashSet::new(),
        all_collections: HashSet::new(),
        all_storage_and_sources: HashMap::new(),
        all_runners_and_sources: HashMap::new(),
        target_paths: HashMap::new(),
        offered_runner_names: HashMap::new(),
        errors: vec![],
    };
    ctx.validate().map_err(|errs| ErrorList::new(errs))
}

/// Validates an independent ChildDecl. Performs the same validation on it as `validate`.
pub fn validate_child(child: &fsys::ChildDecl) -> Result<(), ErrorList> {
    let mut errors = vec![];
    check_name(child.name.as_ref(), "ChildDecl", "name", &mut errors);
    check_url(child.url.as_ref(), "ChildDecl", "url", &mut errors);
    if child.startup.is_none() {
        errors.push(Error::missing_field("ChildDecl", "startup"));
    }
    if errors.is_empty() {
        Ok(())
    } else {
        Err(ErrorList { errs: errors })
    }
}

struct ValidationContext<'a> {
    decl: &'a fsys::ComponentDecl,
    all_children: HashSet<&'a str>,
    all_collections: HashSet<&'a str>,
    all_storage_and_sources: HashMap<&'a str, Option<&'a str>>,
    all_runners_and_sources: HashMap<&'a str, Option<&'a str>>,
    target_paths: PathMap<'a>,
    offered_runner_names: NameMap<'a>,
    errors: Vec<Error>,
}

#[derive(Clone, Copy, PartialEq)]
enum AllowablePaths {
    One,
    Many,
}

#[derive(Debug, PartialEq, Eq, Hash)]
enum TargetId<'a> {
    Component(&'a str),
    Collection(&'a str),
}

type PathMap<'a> = HashMap<TargetId<'a>, HashMap<&'a str, AllowablePaths>>;
type NameMap<'a> = HashMap<TargetId<'a>, HashSet<&'a str>>;

impl<'a> ValidationContext<'a> {
    fn validate(mut self) -> Result<(), Vec<Error>> {
        // Validate "children" and build the set of all children.
        if let Some(children) = self.decl.children.as_ref() {
            for child in children.iter() {
                self.validate_child_decl(&child);
            }
        }

        // Validate "collections" and build the set of all collections.
        if let Some(collections) = self.decl.collections.as_ref() {
            for collection in collections.iter() {
                self.validate_collection_decl(&collection);
            }
        }

        // Validate "storage" and build the set of all storage sections.
        if let Some(storage) = self.decl.storage.as_ref() {
            for storage in storage.iter() {
                self.validate_storage_decl(&storage);
            }
        }

        // Validate "runners" and build the set of all runners.
        if let Some(runners) = self.decl.runners.as_ref() {
            for runner in runners.iter() {
                self.validate_runner_decl(&runner);
            }
        }

        // Validate "uses".
        if let Some(uses) = self.decl.uses.as_ref() {
            self.validate_uses_decl(uses);
        }

        // Validate "exposes".
        if let Some(exposes) = self.decl.exposes.as_ref() {
            let mut target_paths = HashMap::new();
            let mut runner_names = HashSet::new();
            for expose in exposes.iter() {
                self.validate_expose_decl(&expose, &mut target_paths, &mut runner_names);
            }
        }

        // Validate "offers".
        if let Some(offers) = self.decl.offers.as_ref() {
            for offer in offers.iter() {
                self.validate_offers_decl(&offer);
            }
        }

        if self.errors.is_empty() {
            Ok(())
        } else {
            Err(self.errors)
        }
    }

    fn validate_uses_decl(&mut self, uses: &[fsys::UseDecl]) {
        // Validate individual fields.
        for use_ in uses.iter() {
            self.validate_use_decl(&use_);
        }

        // Ensure that no more than one runner is specified.
        let mut runners_count: i32 = 0;
        for use_ in uses.iter() {
            if let fsys::UseDecl::Runner(_) = use_ {
                runners_count += 1;
            }
        }
        if runners_count > 1 {
            self.errors.push(Error::multiple_runners_specified("UseRunnerDecl"));
        }
    }

    fn validate_use_decl(&mut self, use_: &fsys::UseDecl) {
        match use_ {
            fsys::UseDecl::Service(u) => {
                self.validate_use_fields(
                    "UseServiceDecl",
                    u.source.as_ref(),
                    u.source_path.as_ref(),
                    u.target_path.as_ref(),
                );
            }
            fsys::UseDecl::Protocol(u) => {
                self.validate_use_fields(
                    "UseProtocolDecl",
                    u.source.as_ref(),
                    u.source_path.as_ref(),
                    u.target_path.as_ref(),
                );
            }
            fsys::UseDecl::Directory(u) => {
                self.validate_use_fields(
                    "UseDirectoryDecl",
                    u.source.as_ref(),
                    u.source_path.as_ref(),
                    u.target_path.as_ref(),
                );
                if u.rights.is_none() {
                    self.errors.push(Error::missing_field("UseDirectoryDecl", "rights"));
                }
            }
            fsys::UseDecl::Storage(u) => match u.type_ {
                None => self.errors.push(Error::missing_field("UseStorageDecl", "type")),
                Some(fsys::StorageType::Meta) => {
                    if u.target_path.is_some() {
                        self.errors.push(Error::invalid_field("UseStorageDecl", "target_path"));
                    }
                }
                _ => {
                    check_path(
                        u.target_path.as_ref(),
                        "UseStorageDecl",
                        "target_path",
                        &mut self.errors,
                    );
                }
            },
            fsys::UseDecl::Runner(r) => {
                check_name(
                    r.source_name.as_ref(),
                    "UseRunnerDecl",
                    "source_name",
                    &mut self.errors,
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
        source: Option<&fsys::Ref>,
        source_path: Option<&String>,
        target_path: Option<&String>,
    ) {
        match source {
            Some(fsys::Ref::Realm(_)) => {}
            Some(fsys::Ref::Framework(_)) => {}
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "source"));
            }
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
            }
        };
        check_path(source_path, decl, "source_path", &mut self.errors);
        check_path(target_path, decl, "target_path", &mut self.errors);
    }

    fn validate_child_decl(&mut self, child: &'a fsys::ChildDecl) {
        if let Err(mut e) = validate_child(child) {
            self.errors.append(&mut e.errs);
        }
        if let Some(name) = child.name.as_ref() {
            let name: &str = name;
            if !self.all_children.insert(name) {
                self.errors.push(Error::duplicate_field("ChildDecl", "name", name));
            }
        }
    }

    fn validate_collection_decl(&mut self, collection: &'a fsys::CollectionDecl) {
        let name = collection.name.as_ref();
        if check_name(name, "CollectionDecl", "name", &mut self.errors) {
            let name: &str = name.unwrap();
            if !self.all_collections.insert(name) {
                self.errors.push(Error::duplicate_field("CollectionDecl", "name", name));
            }
        }
        if collection.durability.is_none() {
            self.errors.push(Error::missing_field("CollectionDecl", "durability"));
        }
    }

    fn validate_storage_decl(&mut self, storage: &'a fsys::StorageDecl) {
        check_path(storage.source_path.as_ref(), "StorageDecl", "source_path", &mut self.errors);
        let source_child_name = match storage.source.as_ref() {
            Some(fsys::Ref::Realm(_)) => None,
            Some(fsys::Ref::Self_(_)) => None,
            Some(fsys::Ref::Child(child)) => {
                self.validate_source_child(child, "StorageDecl");
                Some(&child.name as &str)
            }
            Some(_) => {
                self.errors.push(Error::invalid_field("StorageDecl", "source"));
                None
            }
            None => {
                self.errors.push(Error::missing_field("StorageDecl", "source"));
                None
            }
        };
        if check_name(storage.name.as_ref(), "StorageDecl", "name", &mut self.errors) {
            let name = storage.name.as_ref().unwrap();
            if self.all_storage_and_sources.insert(name, source_child_name).is_some() {
                self.errors.push(Error::duplicate_field("StorageDecl", "name", name.as_str()));
            }
        }
    }

    fn validate_runner_decl(&mut self, runner: &'a fsys::RunnerDecl) {
        let runner_source = match runner.source.as_ref() {
            Some(fsys::Ref::Self_(_)) => None,
            Some(fsys::Ref::Child(child)) => {
                self.validate_source_child(child, "RunnerDecl");
                Some(&child.name as &str)
            }
            Some(_) => {
                self.errors.push(Error::invalid_field("RunnerDecl", "source"));
                None
            }
            None => {
                self.errors.push(Error::missing_field("RunnerDecl", "source"));
                None
            }
        };
        if check_name(runner.name.as_ref(), "RunnerDecl", "name", &mut self.errors) {
            let name = runner.name.as_ref().unwrap();
            if self.all_runners_and_sources.insert(name, runner_source).is_some() {
                self.errors.push(Error::duplicate_field("RunnerDecl", "name", name.as_str()));
            }
        }
        check_path(runner.source_path.as_ref(), "RunnerDecl", "source_path", &mut self.errors);
    }

    fn validate_source_child(&mut self, child: &fsys::ChildRef, decl_type: &str) {
        let mut valid = true;
        valid &= check_name(Some(&child.name), decl_type, "source.child.name", &mut self.errors);
        valid &= if child.collection.is_some() {
            self.errors.push(Error::extraneous_field(decl_type, "source.child.collection"));
            false
        } else {
            true
        };
        if !valid {
            return;
        }
        if !self.all_children.contains(&child.name as &str) {
            self.errors.push(Error::invalid_child(decl_type, "source", &child.name as &str));
        }
    }

    fn validate_storage_source(&mut self, source: &fsys::StorageRef, decl_type: &str) {
        if check_name(Some(&source.name), decl_type, "source.storage.name", &mut self.errors) {
            if !self.all_storage_and_sources.contains_key(&source.name as &str) {
                self.errors.push(Error::invalid_storage(decl_type, "source", &source.name as &str));
            }
        }
    }

    fn validate_expose_decl(
        &mut self,
        expose: &'a fsys::ExposeDecl,
        prev_target_paths: &mut HashMap<&'a str, AllowablePaths>,
        prev_runner_names: &mut HashSet<&'a str>,
    ) {
        match expose {
            fsys::ExposeDecl::Service(e) => {
                self.validate_expose_fields(
                    "ExposeServiceDecl",
                    AllowablePaths::Many,
                    e.source.as_ref(),
                    e.source_path.as_ref(),
                    e.target_path.as_ref(),
                    e.target.as_ref(),
                    prev_target_paths,
                );
            }
            fsys::ExposeDecl::Protocol(e) => {
                self.validate_expose_fields(
                    "ExposeProtocolDecl",
                    AllowablePaths::One,
                    e.source.as_ref(),
                    e.source_path.as_ref(),
                    e.target_path.as_ref(),
                    e.target.as_ref(),
                    prev_target_paths,
                );
            }
            fsys::ExposeDecl::Directory(e) => {
                self.validate_expose_fields(
                    "ExposeDirectoryDecl",
                    AllowablePaths::One,
                    e.source.as_ref(),
                    e.source_path.as_ref(),
                    e.target_path.as_ref(),
                    e.target.as_ref(),
                    prev_target_paths,
                );

                match e.source.as_ref() {
                    Some(fsys::Ref::Self_(_)) => {
                        if e.rights.is_none() {
                            self.errors.push(Error::missing_field("ExposeDirectoryDecl", "rights"));
                        }
                    }
                    _ => {}
                }
            }
            fsys::ExposeDecl::Runner(e) => {
                self.validate_expose_runner_fields(
                    "ExposeRunnerDecl",
                    e.source.as_ref(),
                    e.source_name.as_ref(),
                    e.target.as_ref(),
                    e.target_name.as_ref(),
                    prev_runner_names,
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
        allowable_paths: AllowablePaths,
        source: Option<&fsys::Ref>,
        source_path: Option<&String>,
        target_path: Option<&'a String>,
        target: Option<&fsys::Ref>,
        prev_child_target_paths: &mut HashMap<&'a str, AllowablePaths>,
    ) {
        match source {
            Some(r) => match r {
                fsys::Ref::Self_(_) => {}
                fsys::Ref::Framework(_) => {}
                fsys::Ref::Child(child) => {
                    self.validate_source_child(child, decl);
                }
                _ => {
                    self.errors.push(Error::invalid_field(decl, "source"));
                }
            },
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
            }
        }
        match target {
            Some(r) => match r {
                fsys::Ref::Realm(_) => {}
                fsys::Ref::Framework(_) => {}
                _ => {
                    self.errors.push(Error::invalid_field(decl, "target"));
                }
            },
            None => {
                self.errors.push(Error::missing_field(decl, "target"));
            }
        }
        check_path(source_path, decl, "source_path", &mut self.errors);
        if check_path(target_path, decl, "target_path", &mut self.errors) {
            let target_path = target_path.unwrap();
            if let Some(prev_state) = prev_child_target_paths.insert(target_path, allowable_paths) {
                if prev_state == AllowablePaths::One || prev_state != allowable_paths {
                    self.errors.push(Error::duplicate_field(decl, "target_path", target_path));
                }
            }
        }
    }

    fn validate_expose_runner_fields(
        &mut self,
        decl: &str,
        source: Option<&fsys::Ref>,
        source_name: Option<&String>,
        target: Option<&fsys::Ref>,
        target_name: Option<&'a String>,
        prev_runner_names: &mut HashSet<&'a str>,
    ) {
        // Ensure our source is valid.
        match source {
            Some(fsys::Ref::Self_(_)) => (),
            Some(fsys::Ref::Child(child)) => self.validate_source_child(child, decl),
            Some(_) => self.errors.push(Error::invalid_field(decl, "source")),
            None => self.errors.push(Error::missing_field(decl, "source")),
        }

        // Ensure our target is valid.
        match target {
            Some(fsys::Ref::Realm(_)) => {}
            Some(_) => self.errors.push(Error::invalid_field(decl, "target")),
            None => self.errors.push(Error::missing_field(decl, "target")),
        }

        // Ensure source name is valid.
        check_name(source_name, decl, "source_name", &mut self.errors);

        // Ensure target_name is valid.
        if check_name(target_name, decl, "target_name", &mut self.errors) {
            // Ensure that target_name hasn't already been exposed.
            if !prev_runner_names.insert(target_name.unwrap()) {
                self.errors.push(Error::duplicate_field(decl, "target_name", target_name.unwrap()));
            }
        }

        // If the expose source is ourself, ensure we have a corresponding RunnerDecl.
        if let (Some(fsys::Ref::Self_(_)), Some(name)) = (source, source_name) {
            if !self.all_runners_and_sources.contains_key(&name as &str) {
                self.errors.push(Error::invalid_field(decl, "source"));
            }
        }
    }

    fn validate_offers_decl(&mut self, offer: &'a fsys::OfferDecl) {
        match offer {
            fsys::OfferDecl::Service(o) => {
                self.validate_offers_fields(
                    "OfferServiceDecl",
                    AllowablePaths::Many,
                    o.source.as_ref(),
                    o.source_path.as_ref(),
                    o.target.as_ref(),
                    o.target_path.as_ref(),
                );
            }
            fsys::OfferDecl::Protocol(o) => {
                self.validate_offers_fields(
                    "OfferProtocolDecl",
                    AllowablePaths::One,
                    o.source.as_ref(),
                    o.source_path.as_ref(),
                    o.target.as_ref(),
                    o.target_path.as_ref(),
                );
            }
            fsys::OfferDecl::Directory(o) => {
                self.validate_offers_fields(
                    "OfferDirectoryDecl",
                    AllowablePaths::One,
                    o.source.as_ref(),
                    o.source_path.as_ref(),
                    o.target.as_ref(),
                    o.target_path.as_ref(),
                );
                match o.source.as_ref() {
                    Some(fsys::Ref::Self_(_)) => {
                        if o.rights.is_none() {
                            self.errors.push(Error::missing_field("OfferDirectoryDecl", "rights"));
                        }
                    }
                    _ => {}
                }
            }
            fsys::OfferDecl::Storage(o) => {
                self.validate_storage_offer_fields(
                    "OfferStorageDecl",
                    o.type_.as_ref(),
                    o.source.as_ref(),
                    o.target.as_ref(),
                );
            }
            fsys::OfferDecl::Runner(o) => {
                self.validate_runner_offer_fields(
                    "OfferRunnerDecl",
                    o.source.as_ref(),
                    o.source_name.as_ref(),
                    o.target.as_ref(),
                    o.target_name.as_ref(),
                );
            }
            fsys::OfferDecl::__UnknownVariant { .. } => {
                self.errors.push(Error::invalid_field("ComponentDecl", "offer"));
            }
        }
    }

    fn validate_offers_fields(
        &mut self,
        decl: &str,
        allowable_paths: AllowablePaths,
        source: Option<&fsys::Ref>,
        source_path: Option<&String>,
        target: Option<&'a fsys::Ref>,
        target_path: Option<&'a String>,
    ) {
        match source {
            Some(fsys::Ref::Realm(_)) => {}
            Some(fsys::Ref::Self_(_)) => {}
            Some(fsys::Ref::Framework(_)) => {}
            Some(fsys::Ref::Child(child)) => self.validate_source_child(child, decl),
            Some(_) => self.errors.push(Error::invalid_field(decl, "source")),
            None => self.errors.push(Error::missing_field(decl, "source")),
        }
        check_path(source_path, decl, "source_path", &mut self.errors);
        match target {
            Some(fsys::Ref::Child(c)) => {
                self.validate_target_child(decl, allowable_paths, c, source, target_path);
            }
            Some(fsys::Ref::Collection(c)) => {
                self.validate_target_collection(decl, allowable_paths, c, target_path);
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "target"));
            }
            None => {
                self.errors.push(Error::missing_field(decl, "target"));
            }
        }
        check_path(target_path, decl, "target_path", &mut self.errors);
    }

    fn validate_storage_offer_fields(
        &mut self,
        decl: &str,
        type_: Option<&fsys::StorageType>,
        source: Option<&'a fsys::Ref>,
        target: Option<&'a fsys::Ref>,
    ) {
        if type_.is_none() {
            self.errors.push(Error::missing_field(decl, "type"));
        }
        let storage_source_name = match source {
            Some(fsys::Ref::Realm(_)) => None,
            Some(fsys::Ref::Storage(s)) => {
                self.validate_storage_source(s, decl);
                Some(&s.name as &str)
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "source"));
                None
            }
            None => {
                self.errors.push(Error::missing_field(decl, "source"));
                None
            }
        };
        self.validate_storage_target(decl, storage_source_name, target);
    }

    fn validate_runner_offer_fields(
        &mut self,
        decl: &str,
        source: Option<&fsys::Ref>,
        source_name: Option<&String>,
        target: Option<&'a fsys::Ref>,
        target_name: Option<&'a String>,
    ) {
        // Validate source.
        match source {
            Some(fsys::Ref::Realm(_)) => {}
            Some(fsys::Ref::Self_(_)) => {}
            Some(fsys::Ref::Child(child)) => self.validate_source_child(child, decl),
            Some(_) => self.errors.push(Error::invalid_field(decl, "source")),
            None => self.errors.push(Error::missing_field(decl, "source")),
        }
        check_name(source_name, decl, "source_name", &mut self.errors);

        // Validate target.
        let target_key = match target {
            Some(fsys::Ref::Child(c)) => {
                self.validate_child_ref(decl, "target", &c);
                Some(TargetId::Component(&c.name))
            }
            Some(fsys::Ref::Collection(c)) => {
                self.validate_collection_ref(decl, "target", &c);
                Some(TargetId::Collection(&c.name))
            }
            Some(_) => {
                self.errors.push(Error::invalid_field(decl, "target"));
                None
            }
            None => {
                self.errors.push(Error::missing_field(decl, "target"));
                None
            }
        };
        check_name(target_name, decl, "target_name", &mut self.errors);

        // Assuming the target is valid, ensure the target runner name isn't already used.
        if let (Some(target_key), Some(target_name)) = (target_key, target_name) {
            if !self
                .offered_runner_names
                .entry(target_key)
                .or_insert(HashSet::new())
                .insert(target_name)
            {
                self.errors.push(Error::duplicate_field(decl, "target_name", target_name as &str));
            }
        };

        // Ensure source != target.
        match (source, target) {
            (Some(fsys::Ref::Child(source_child)), Some(fsys::Ref::Child(target_child))) => {
                if source_child.name == target_child.name {
                    self.errors
                        .push(Error::offer_target_equals_source(decl, &target_child.name as &str));
                }
            }
            (_, _) => (),
        }
    }

    /// Check a `ChildRef` contains a valid child that exists.
    ///
    /// We ensure the target child is statically defined (i.e., not a dynamic child inside
    /// a collection).
    fn validate_child_ref(&mut self, decl: &str, field_name: &str, child: &fsys::ChildRef) -> bool {
        // Ensure the name is valid, and the reference refers to a static child.
        //
        // We attempt to list all errors if possible.
        let mut valid = true;
        if !check_name(
            Some(&child.name),
            decl,
            &format!("{}.child.name", field_name),
            &mut self.errors,
        ) {
            valid = false;
        }
        if child.collection.is_some() {
            self.errors
                .push(Error::extraneous_field(decl, format!("{}.child.collection", field_name)));
            valid = false;
        }
        if !valid {
            return false;
        }

        // Ensure the child exists.
        let name: &str = &child.name;
        if !self.all_children.contains(name) {
            self.errors.push(Error::invalid_child(decl, field_name, name));
            return false;
        }

        true
    }

    /// Check a `CollectionRef` is valid and refers to an existing collection.
    fn validate_collection_ref(
        &mut self,
        decl: &str,
        field_name: &str,
        collection: &fsys::CollectionRef,
    ) -> bool {
        // Ensure the name is valid.
        if !check_name(
            Some(&collection.name),
            decl,
            &format!("{}.collection.name", field_name),
            &mut self.errors,
        ) {
            return false;
        }

        // Ensure the collection exists.
        if !self.all_collections.contains(&collection.name as &str) {
            self.errors.push(Error::invalid_collection(decl, field_name, &collection.name as &str));
            return false;
        }

        true
    }

    fn validate_target_child(
        &mut self,
        decl: &str,
        allowable_paths: AllowablePaths,
        child: &'a fsys::ChildRef,
        source: Option<&fsys::Ref>,
        target_path: Option<&'a String>,
    ) {
        if !self.validate_child_ref(decl, "target", child) {
            return;
        }
        if let Some(target_path) = target_path {
            let paths_for_target =
                self.target_paths.entry(TargetId::Component(&child.name)).or_insert(HashMap::new());
            if let Some(prev_state) = paths_for_target.insert(target_path, allowable_paths) {
                if prev_state == AllowablePaths::One || prev_state != allowable_paths {
                    self.errors.push(Error::duplicate_field(
                        decl,
                        "target_path",
                        target_path as &str,
                    ));
                }
            }
            if let Some(source) = source {
                if let fsys::Ref::Child(source_child) = source {
                    if source_child.name == child.name {
                        self.errors
                            .push(Error::offer_target_equals_source(decl, &child.name as &str));
                    }
                }
            }
        }
    }

    fn validate_target_collection(
        &mut self,
        decl: &str,
        allowable_paths: AllowablePaths,
        collection: &'a fsys::CollectionRef,
        target_path: Option<&'a String>,
    ) {
        if !self.validate_collection_ref(decl, "target", &collection) {
            return;
        }
        if let Some(target_path) = target_path {
            let paths_for_target = self
                .target_paths
                .entry(TargetId::Collection(&collection.name))
                .or_insert(HashMap::new());
            if let Some(prev_state) = paths_for_target.insert(target_path, allowable_paths) {
                if prev_state == AllowablePaths::One || prev_state != allowable_paths {
                    self.errors.push(Error::duplicate_field(
                        decl,
                        "target_path",
                        target_path as &str,
                    ));
                }
            }
        }
    }

    fn validate_storage_target(
        &mut self,
        decl: &str,
        storage_source_name: Option<&'a str>,
        target: Option<&'a fsys::Ref>,
    ) {
        match target {
            Some(fsys::Ref::Child(c)) => {
                if !self.validate_child_ref(decl, "target", &c) {
                    return;
                }
                let name = &c.name;
                if let Some(source_name) = storage_source_name {
                    if self.all_storage_and_sources.get(source_name) == Some(&Some(name)) {
                        self.errors.push(Error::offer_target_equals_source(decl, name));
                    }
                }
            }
            Some(fsys::Ref::Collection(c)) => {
                self.validate_collection_ref(decl, "target", &c);
            }
            Some(_) => self.errors.push(Error::invalid_field(decl, "target")),
            None => self.errors.push(Error::missing_field(decl, "target")),
        }
    }
}

fn check_presence_and_length(
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

fn check_path(
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

fn check_name(
    prop: Option<&String>,
    decl_type: &str,
    keyword: &str,
    errors: &mut Vec<Error>,
) -> bool {
    let start_err_len = errors.len();
    check_presence_and_length(MAX_NAME_LENGTH, prop, decl_type, keyword, errors);
    if let Some(name) = prop {
        for b in name.bytes() {
            match b as char {
                '0'..='9' | 'a'..='z' | '_' | '-' | '.' => (),
                c => {
                    errors.push(Error::invalid_character_in_field(decl_type, keyword, c));
                    return false;
                }
            }
        }
    }
    start_err_len == errors.len()
}

fn check_url(
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
                c => {
                    errors.push(Error::invalid_character_in_field(decl_type, keyword, c));
                    return false;
                }
            }
        }
        // If we've reached here then the string terminated unexpectedly
        errors.push(Error::invalid_field(decl_type, keyword));
    }
    start_err_len == errors.len()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_io2 as fio2,
        fidl_fuchsia_sys2::{
            ChildDecl, ChildRef, CollectionDecl, CollectionRef, ComponentDecl, Durability,
            ExposeDecl, ExposeDirectoryDecl, ExposeProtocolDecl, ExposeRunnerDecl,
            ExposeServiceDecl, FrameworkRef, OfferDecl, OfferDirectoryDecl, OfferProtocolDecl,
            OfferRunnerDecl, OfferServiceDecl, OfferStorageDecl, RealmRef, Ref, RunnerDecl,
            SelfRef, StartupMode, StorageDecl, StorageRef, StorageType, UseDecl, UseDirectoryDecl,
            UseProtocolDecl, UseRunnerDecl, UseServiceDecl, UseStorageDecl,
        },
        lazy_static::lazy_static,
        proptest::prelude::*,
        regex::Regex,
    };

    const PATH_REGEX_STR: &str = r"(/[^/]+)+";
    const NAME_REGEX_STR: &str = r"[0-9a-z_\-\.]+";
    const URL_REGEX_STR: &str = r"[0-9a-z\+\-\.]+://.+";

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

    fn validate_test(input: ComponentDecl, expected_res: Result<(), ErrorList>) {
        let res = validate(&input);
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
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

    fn new_component_decl() -> ComponentDecl {
        ComponentDecl {
            program: None,
            uses: None,
            exposes: None,
            offers: None,
            facets: None,
            storage: None,
            children: None,
            collections: None,
            runners: None,
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
        assert_eq!(
            format!("{}", Error::multiple_runners_specified("Decl")),
            "Decl specifies multiple runners"
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
            result = Err(ErrorList::new(vec![Error::invalid_character_in_field("FooDecl", "foo", '^')])),
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

    test_validate! {
        // uses
        test_validate_uses_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                    }),
                    UseDecl::Protocol(UseProtocolDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                        rights: None,
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        type_: None,
                        target_path: None,
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        type_: Some(StorageType::Cache),
                        target_path: None,
                    }),
                    UseDecl::Runner(UseRunnerDecl {
                        source_name: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("UseServiceDecl", "source"),
                Error::missing_field("UseServiceDecl", "source_path"),
                Error::missing_field("UseServiceDecl", "target_path"),
                Error::missing_field("UseProtocolDecl", "source"),
                Error::missing_field("UseProtocolDecl", "source_path"),
                Error::missing_field("UseProtocolDecl", "target_path"),
                Error::missing_field("UseDirectoryDecl", "source"),
                Error::missing_field("UseDirectoryDecl", "source_path"),
                Error::missing_field("UseDirectoryDecl", "target_path"),
                Error::missing_field("UseDirectoryDecl", "rights"),
                Error::missing_field("UseStorageDecl", "type"),
                Error::missing_field("UseStorageDecl", "target_path"),
                Error::missing_field("UseRunnerDecl", "source_name"),
            ])),
        },
        test_validate_uses_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                    UseDecl::Protocol(UseProtocolDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        type_: Some(StorageType::Cache),
                        target_path: Some("/".to_string()),
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        type_: Some(StorageType::Meta),
                        target_path: Some("/meta".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("UseServiceDecl", "source"),
                Error::invalid_field("UseServiceDecl", "source_path"),
                Error::invalid_field("UseServiceDecl", "target_path"),
                Error::invalid_field("UseProtocolDecl", "source"),
                Error::invalid_field("UseProtocolDecl", "source_path"),
                Error::invalid_field("UseProtocolDecl", "target_path"),
                Error::invalid_field("UseDirectoryDecl", "source"),
                Error::invalid_field("UseDirectoryDecl", "source_path"),
                Error::invalid_field("UseDirectoryDecl", "target_path"),
                Error::invalid_field("UseStorageDecl", "target_path"),
                Error::invalid_field("UseStorageDecl", "target_path"),
            ])),
        },
        test_validate_uses_multiple_runners => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Runner(UseRunnerDecl {
                        source_name: Some("elf".to_string()),
                    }),
                    UseDecl::Runner(UseRunnerDecl {
                        source_name: Some("elf".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::multiple_runners_specified("UseRunnerDecl"),
            ])),
        },
        test_validate_uses_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.uses = Some(vec![
                    UseDecl::Service(UseServiceDecl {
                        source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    UseDecl::Protocol(UseProtocolDecl {
                        source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    UseDecl::Directory(UseDirectoryDecl {
                        source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    UseDecl::Storage(UseStorageDecl {
                        type_: Some(StorageType::Cache),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    UseDecl::Runner(UseRunnerDecl {
                        source_name: Some(format!("{}", "a".repeat(101))),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("UseServiceDecl", "source_path"),
                Error::field_too_long("UseServiceDecl", "target_path"),
                Error::field_too_long("UseProtocolDecl", "source_path"),
                Error::field_too_long("UseProtocolDecl", "target_path"),
                Error::field_too_long("UseDirectoryDecl", "source_path"),
                Error::field_too_long("UseDirectoryDecl", "target_path"),
                Error::field_too_long("UseStorageDecl", "target_path"),
                Error::field_too_long("UseRunnerDecl", "source_name"),
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
                        target: None,
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                        target: None,
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: None,
                        source_path: None,
                        target_path: None,
                        target: None,
                        rights: None,
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ExposeServiceDecl", "source"),
                Error::missing_field("ExposeServiceDecl", "target"),
                Error::missing_field("ExposeServiceDecl", "source_path"),
                Error::missing_field("ExposeServiceDecl", "target_path"),
                Error::missing_field("ExposeProtocolDecl", "source"),
                Error::missing_field("ExposeProtocolDecl", "target"),
                Error::missing_field("ExposeProtocolDecl", "source_path"),
                Error::missing_field("ExposeProtocolDecl", "target_path"),
                Error::missing_field("ExposeDirectoryDecl", "source"),
                Error::missing_field("ExposeDirectoryDecl", "target"),
                Error::missing_field("ExposeDirectoryDecl", "source_path"),
                Error::missing_field("ExposeDirectoryDecl", "target_path"),
                Error::missing_field("ExposeRunnerDecl", "source"),
                Error::missing_field("ExposeRunnerDecl", "target"),
                Error::missing_field("ExposeRunnerDecl", "source_name"),
                Error::missing_field("ExposeRunnerDecl", "target_name"),
            ])),
        },
        test_validate_exposes_extraneous => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/svc/logger".to_string()),
                        target_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/svc/legacy_logger".to_string()),
                        target_path: Some("/svc/legacy_logger".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/data".to_string()),
                        target_path: Some("/data".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::extraneous_field("ExposeServiceDecl", "source.child.collection"),
                Error::extraneous_field("ExposeProtocolDecl", "source.child.collection"),
                Error::extraneous_field("ExposeDirectoryDecl", "source.child.collection"),
                Error::extraneous_field("ExposeRunnerDecl", "source.child.collection"),
            ])),
        },
        test_validate_exposes_rights => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/a".to_string()),
                        target_path: Some("/data/b".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        rights: None,
                    })
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ExposeDirectoryDecl", "rights"),
            ])),
        },
        test_validate_exposes_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_path: Some("foo/".to_string()),
                        target_path: Some("/".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "^bad".to_string(),
                            collection: None,
                        })),
                        source_name: Some("/path".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf!".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_character_in_field("ExposeServiceDecl", "source.child.name", '^'),
                Error::invalid_field("ExposeServiceDecl", "source_path"),
                Error::invalid_field("ExposeServiceDecl", "target_path"),
                Error::invalid_character_in_field("ExposeProtocolDecl", "source.child.name", '^'),
                Error::invalid_field("ExposeProtocolDecl", "source_path"),
                Error::invalid_field("ExposeProtocolDecl", "target_path"),
                Error::invalid_character_in_field("ExposeDirectoryDecl", "source.child.name", '^'),
                Error::invalid_field("ExposeDirectoryDecl", "source_path"),
                Error::invalid_field("ExposeDirectoryDecl", "target_path"),
                Error::invalid_character_in_field("ExposeRunnerDecl", "source.child.name", '^'),
                Error::invalid_character_in_field("ExposeRunnerDecl", "source_name", '/'),
                Error::invalid_character_in_field("ExposeRunnerDecl", "target_name", '!'),
            ])),
        },
        test_validate_exposes_invalid_source_target => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: None,
                        source_path: Some("/a".to_string()),
                        target_path: Some("/b".to_string()),
                        target: None,
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Realm(RealmRef {})),
                        source_path: Some("/c".to_string()),
                        target_path: Some("/d".to_string()),
                        target: Some(Ref::Self_(SelfRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Collection(CollectionRef {name: "z".to_string()})),
                        source_path: Some("/e".to_string()),
                        target_path: Some("/f".to_string()),
                        target: Some(Ref::Collection(CollectionRef {name: "z".to_string()})),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Storage(StorageRef {name: "a".to_string()})),
                        source_path: Some("/g".to_string()),
                        target_path: Some("/h".to_string()),
                        target: Some(Ref::Storage(StorageRef {name: "a".to_string()})),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Storage(StorageRef {name: "a".to_string()})),
                        source_name: Some("a".to_string()),
                        target: Some(Ref::Framework(FrameworkRef {})),
                        target_name: Some("b".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ExposeServiceDecl", "source"),
                Error::missing_field("ExposeServiceDecl", "target"),
                Error::invalid_field("ExposeProtocolDecl", "source"),
                Error::invalid_field("ExposeProtocolDecl", "target"),
                Error::invalid_field("ExposeDirectoryDecl", "source"),
                Error::invalid_field("ExposeDirectoryDecl", "target"),
                Error::invalid_field("ExposeDirectoryDecl", "source"),
                Error::invalid_field("ExposeDirectoryDecl", "target"),
                Error::invalid_field("ExposeRunnerDecl", "source"),
                Error::invalid_field("ExposeRunnerDecl", "target"),
            ])),
        },
        test_validate_exposes_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        target: Some(Ref::Realm(RealmRef {})),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("a".repeat(101)),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("b".repeat(101)),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ExposeServiceDecl", "source.child.name"),
                Error::field_too_long("ExposeServiceDecl", "source_path"),
                Error::field_too_long("ExposeServiceDecl", "target_path"),
                Error::field_too_long("ExposeProtocolDecl", "source.child.name"),
                Error::field_too_long("ExposeProtocolDecl", "source_path"),
                Error::field_too_long("ExposeProtocolDecl", "target_path"),
                Error::field_too_long("ExposeDirectoryDecl", "source.child.name"),
                Error::field_too_long("ExposeDirectoryDecl", "source_path"),
                Error::field_too_long("ExposeDirectoryDecl", "target_path"),
                Error::field_too_long("ExposeRunnerDecl", "source.child.name"),
                Error::field_too_long("ExposeRunnerDecl", "source_name"),
                Error::field_too_long("ExposeRunnerDecl", "target_name"),
            ])),
        },
        test_validate_exposes_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/loggers/fuchsia.logger.LegacyLog".to_string()),
                        target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/data/netstack".to_string()),
                        target_path: Some("/data".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "netstack".to_string(),
                            collection: None,
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("ExposeServiceDecl", "source", "netstack"),
                Error::invalid_child("ExposeProtocolDecl", "source", "netstack"),
                Error::invalid_child("ExposeDirectoryDecl", "source", "netstack"),
                Error::invalid_child("ExposeRunnerDecl", "source", "netstack"),
            ])),
        },
        test_validate_exposes_duplicate_target => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![RunnerDecl{
                    name: Some("source_elf".to_string()),
                    source: Some(Ref::Self_(SelfRef{})),
                    source_path: Some("/path".to_string()),
                }]);
                decl.exposes = Some(vec![
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger2".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger3".to_string()),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/netstack".to_string()),
                        target_path: Some("/svc/fuchsia.net.Stack".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Service(ExposeServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/netstack2".to_string()),
                        target_path: Some("/svc/fuchsia.net.Stack".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("ExposeServiceDecl", "target_path",
                                       "/svc/fuchsia.logger.Log"),
                Error::duplicate_field("ExposeDirectoryDecl", "target_path",
                                       "/svc/fuchsia.logger.Log"),
                Error::duplicate_field("ExposeRunnerDecl", "target_name",
                                       "elf"),
            ])),
        },
        test_validate_exposes_invalid_runner_from_self => {
            input = {
                let mut decl = new_component_decl();
                decl.exposes = Some(vec![
                    ExposeDecl::Runner(ExposeRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("source_elf".to_string()),
                        target: Some(Ref::Realm(RealmRef {})),
                        target_name: Some("elf".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                // We are attempting to expose a runner from "self", but we don't
                // acutally declare a runner.
                Error::invalid_field("ExposeRunnerDecl", "source"),
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
                        target: None,
                        target_path: None,
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: None,
                        source_path: None,
                        target: None,
                        target_path: None,
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: None,
                        source_path: None,
                        target: None,
                        target_path: None,
                        rights: None,
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        type_: None,
                        source: None,
                        target: None,
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: None,
                        source_name: None,
                        target: None,
                        target_name: None,
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("OfferServiceDecl", "source"),
                Error::missing_field("OfferServiceDecl", "source_path"),
                Error::missing_field("OfferServiceDecl", "target"),
                Error::missing_field("OfferServiceDecl", "target_path"),
                Error::missing_field("OfferProtocolDecl", "source"),
                Error::missing_field("OfferProtocolDecl", "source_path"),
                Error::missing_field("OfferProtocolDecl", "target"),
                Error::missing_field("OfferProtocolDecl", "target_path"),
                Error::missing_field("OfferDirectoryDecl", "source"),
                Error::missing_field("OfferDirectoryDecl", "source_path"),
                Error::missing_field("OfferDirectoryDecl", "target"),
                Error::missing_field("OfferDirectoryDecl", "target_path"),
                Error::missing_field("OfferStorageDecl", "type"),
                Error::missing_field("OfferStorageDecl", "source"),
                Error::missing_field("OfferStorageDecl", "target"),
                Error::missing_field("OfferRunnerDecl", "source"),
                Error::missing_field("OfferRunnerDecl", "source_name"),
                Error::missing_field("OfferRunnerDecl", "target"),
                Error::missing_field("OfferRunnerDecl", "target_name"),
            ])),
        },
        test_validate_offers_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "b".repeat(101),
                               collection: None,
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef {})),
                        source_path: Some("/a".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef {
                               name: "b".repeat(101),
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "b".repeat(101),
                               collection: None,
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Self_(SelfRef {})),
                        source_path: Some("/a".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef {
                               name: "b".repeat(101),
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "a".repeat(1024))),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "b".repeat(101),
                               collection: None,
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef {})),
                        source_path: Some("/a".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef {
                               name: "b".repeat(101),
                           }
                        )),
                        target_path: Some(format!("/{}", "b".repeat(1024))),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Realm(RealmRef {})),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "b".repeat(101),
                                collection: None,
                            }
                        )),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Realm(RealmRef {})),
                        target: Some(Ref::Collection(
                            CollectionRef { name: "b".repeat(101) }
                        )),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "a".repeat(101),
                            collection: None,
                        })),
                        source_name: Some("b".repeat(101)),
                        target: Some(Ref::Collection(
                           CollectionRef {
                               name: "c".repeat(101),
                           }
                        )),
                        target_name: Some("d".repeat(101)),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("OfferServiceDecl", "source.child.name"),
                Error::field_too_long("OfferServiceDecl", "source_path"),
                Error::field_too_long("OfferServiceDecl", "target.child.name"),
                Error::field_too_long("OfferServiceDecl", "target_path"),
                Error::field_too_long("OfferServiceDecl", "target.collection.name"),
                Error::field_too_long("OfferServiceDecl", "target_path"),
                Error::field_too_long("OfferProtocolDecl", "source.child.name"),
                Error::field_too_long("OfferProtocolDecl", "source_path"),
                Error::field_too_long("OfferProtocolDecl", "target.child.name"),
                Error::field_too_long("OfferProtocolDecl", "target_path"),
                Error::field_too_long("OfferProtocolDecl", "target.collection.name"),
                Error::field_too_long("OfferProtocolDecl", "target_path"),
                Error::field_too_long("OfferDirectoryDecl", "source.child.name"),
                Error::field_too_long("OfferDirectoryDecl", "source_path"),
                Error::field_too_long("OfferDirectoryDecl", "target.child.name"),
                Error::field_too_long("OfferDirectoryDecl", "target_path"),
                Error::field_too_long("OfferDirectoryDecl", "target.collection.name"),
                Error::field_too_long("OfferDirectoryDecl", "target_path"),
                Error::field_too_long("OfferStorageDecl", "target.child.name"),
                Error::field_too_long("OfferStorageDecl", "target.collection.name"),
                Error::field_too_long("OfferRunnerDecl", "source.child.name"),
                Error::field_too_long("OfferRunnerDecl", "source_name"),
                Error::field_too_long("OfferRunnerDecl", "target.collection.name"),
                Error::field_too_long("OfferRunnerDecl", "target_name"),
            ])),
        },
        test_validate_offers_rights => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![OfferDecl::Directory(OfferDirectoryDecl {
                    source: Some(Ref::Self_(SelfRef{})),
                    source_path: Some("/data/assets".to_string()),
                    target: Some(Ref::Child(
                       ChildRef {
                           name: "logger".to_string(),
                           collection: None,
                       }
                    )),
                    target_path: Some("/data".to_string()),
                    rights: None,
                })]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("OfferDirectoryDecl", "target", "logger"),
                Error::missing_field("OfferDirectoryDecl", "rights"),
            ])),
        },
        test_validate_offers_extraneous => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_path: Some("/data/realm_assets".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_path: Some("/data/realm_assets".to_string()),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Realm(RealmRef{ })),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: Some("modular".to_string()),
                        })),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: Some("modular".to_string()),
                            }
                        )),
                        target_name: Some("elf".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::extraneous_field("OfferServiceDecl", "source.child.collection"),
                Error::extraneous_field("OfferServiceDecl", "target.child.collection"),
                Error::extraneous_field("OfferProtocolDecl", "source.child.collection"),
                Error::extraneous_field("OfferProtocolDecl", "target.child.collection"),
                Error::extraneous_field("OfferDirectoryDecl", "source.child.collection"),
                Error::extraneous_field("OfferDirectoryDecl", "target.child.collection"),
                Error::extraneous_field("OfferStorageDecl", "target.child.collection"),
                Error::extraneous_field("OfferRunnerDecl", "source.child.collection"),
                Error::extraneous_field("OfferRunnerDecl", "target.child.collection"),
            ])),
        },
        test_validate_offers_target_equals_source => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/logger".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/svc/legacy_logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/legacy_logger".to_string()),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_name: Some("web".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "logger".to_string(),
                               collection: None,
                           }
                        )),
                        target_name: Some("web".to_string()),
                    }),
                ]);
                decl.children = Some(vec![ChildDecl{
                    name: Some("logger".to_string()),
                    url: Some("fuchsia-pkg://fuchsia.com/logger#meta/logger.cm".to_string()),
                    startup: Some(StartupMode::Lazy),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::offer_target_equals_source("OfferServiceDecl", "logger"),
                Error::offer_target_equals_source("OfferProtocolDecl", "logger"),
                Error::offer_target_equals_source("OfferDirectoryDecl", "logger"),
                Error::offer_target_equals_source("OfferRunnerDecl", "logger"),
            ])),
        },
        test_validate_offers_storage_target_equals_source => {
            input = ComponentDecl {
                offers: Some(vec![
                    OfferDecl::Storage(OfferStorageDecl {
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Storage(StorageRef {
                            name: "minfs".to_string(),
                        })),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "logger".to_string(),
                                collection: None,
                            }
                        )),
                    })
                ]),
                children: Some(vec![
                    ChildDecl {
                        name: Some("logger".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                        startup: Some(StartupMode::Lazy),
                    },
                ]),
                storage: Some(vec![
                    StorageDecl {
                        name: Some("minfs".to_string()),
                        source_path: Some("/minfs".to_string()),
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                    }
                ]),
                ..new_component_decl()
            },
            result = Err(ErrorList::new(vec![
                Error::offer_target_equals_source("OfferStorageDecl", "logger"),
            ])),
        },
        test_validate_offers_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/loggers/fuchsia.logger.Log".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/data/realm_assets".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/loggers/fuchsia.logger.LegacyLog".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/data/legacy_realm_assets".to_string()),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                ]);
                decl.storage = Some(vec![
                    StorageDecl {
                        name: Some("memfs".to_string()),
                        source_path: Some("/memfs".to_string()),
                        source: Some(Ref::Child(ChildRef {
                            name: "logger".to_string(),
                            collection: None,
                        })),
                    },
                ]);
                decl.children = Some(vec![
                    ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(StartupMode::Lazy),
                    },
                ]);
                decl.collections = Some(vec![
                    CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(Durability::Persistent),
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("StorageDecl", "source", "logger"),
                Error::invalid_child("OfferServiceDecl", "source", "logger"),
                Error::invalid_child("OfferProtocolDecl", "source", "logger"),
                Error::invalid_child("OfferDirectoryDecl", "source", "logger"),
            ])),
        },
        test_validate_offers_target_duplicate_path => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("/data".to_string()),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string() }
                        )),
                        target_name: Some("duplicated".to_string()),
                    }),
                ]);
                decl.children = Some(vec![
                    ChildDecl{
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                        startup: Some(StartupMode::Eager),
                    },
                ]);
                decl.collections = Some(vec![
                    CollectionDecl{
                        name: Some("modular".to_string()),
                        durability: Some(Durability::Persistent),
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("OfferServiceDecl", "target_path", "/data"),
                Error::duplicate_field("OfferDirectoryDecl", "target_path", "/data"),
                Error::duplicate_field("OfferRunnerDecl", "target_name", "duplicated"),
            ])),
        },
        test_validate_offers_target_invalid => {
            input = {
                let mut decl = new_component_decl();
                decl.offers = Some(vec![
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Service(OfferServiceDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/logger".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/legacy_logger".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                    }),
                    OfferDecl::Protocol(OfferProtocolDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/svc/legacy_logger".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_path: Some("/svc/fuchsia.logger.LegacyLog".to_string()),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Child(
                           ChildRef {
                               name: "netstack".to_string(),
                               collection: None,
                           }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    OfferDecl::Directory(OfferDirectoryDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/data/assets".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_path: Some("/data".to_string()),
                        rights: Some(fio2::Operations::Connect),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Realm(RealmRef{})),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                    }),
                    OfferDecl::Storage(OfferStorageDecl {
                        type_: Some(StorageType::Data),
                        source: Some(Ref::Realm(RealmRef{})),
                        target: Some(Ref::Collection(
                            CollectionRef { name: "modular".to_string(), }
                        )),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Child(
                            ChildRef {
                                name: "netstack".to_string(),
                                collection: None,
                            }
                        )),
                        target_name: Some("elf".to_string()),
                    }),
                    OfferDecl::Runner(OfferRunnerDecl {
                        source: Some(Ref::Self_(SelfRef{})),
                        source_name: Some("elf".to_string()),
                        target: Some(Ref::Collection(
                           CollectionRef { name: "modular".to_string(), }
                        )),
                        target_name: Some("elf".to_string()),
                    }),
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_child("OfferServiceDecl", "target", "netstack"),
                Error::invalid_collection("OfferServiceDecl", "target", "modular"),
                Error::invalid_child("OfferProtocolDecl", "target", "netstack"),
                Error::invalid_collection("OfferProtocolDecl", "target", "modular"),
                Error::invalid_child("OfferDirectoryDecl", "target", "netstack"),
                Error::invalid_collection("OfferDirectoryDecl", "target", "modular"),
                Error::invalid_child("OfferStorageDecl", "target", "netstack"),
                Error::invalid_collection("OfferStorageDecl", "target", "modular"),
                Error::invalid_child("OfferRunnerDecl", "target", "netstack"),
                Error::invalid_collection("OfferRunnerDecl", "target", "modular"),
            ])),
        },

        // children
        test_validate_children_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: None,
                    url: None,
                    startup: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("ChildDecl", "name"),
                Error::missing_field("ChildDecl", "url"),
                Error::missing_field("ChildDecl", "startup"),
            ])),
        },
        test_validate_children_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: Some("^bad".to_string()),
                    url: Some("bad-scheme&://blah".to_string()),
                    startup: Some(StartupMode::Lazy),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_character_in_field("ChildDecl", "name", '^'),
                Error::invalid_character_in_field("ChildDecl", "url", '&'),
            ])),
        },
        test_validate_children_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.children = Some(vec![ChildDecl{
                    name: Some("a".repeat(1025)),
                    url: Some(format!("fuchsia-pkg://{}", "a".repeat(4083))),
                    startup: Some(StartupMode::Lazy),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("ChildDecl", "name"),
                Error::field_too_long("ChildDecl", "url"),
            ])),
        },

        // collections
        test_validate_collections_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![CollectionDecl{
                    name: None,
                    durability: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("CollectionDecl", "name"),
                Error::missing_field("CollectionDecl", "durability"),
            ])),
        },
        test_validate_collections_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![CollectionDecl{
                    name: Some("^bad".to_string()),
                    durability: Some(Durability::Persistent),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_character_in_field("CollectionDecl", "name", '^'),
            ])),
        },
        test_validate_collections_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.collections = Some(vec![CollectionDecl{
                    name: Some("a".repeat(1025)),
                    durability: Some(Durability::Transient),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("CollectionDecl", "name"),
            ])),
        },

        // storage
        test_validate_storage_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.storage = Some(vec![StorageDecl{
                    name: Some("a".repeat(101)),
                    source_path: Some(format!("/{}", "a".repeat(1024))),
                    source: Some(Ref::Self_(SelfRef{})),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("StorageDecl", "source_path"),
                Error::field_too_long("StorageDecl", "name"),
            ])),
        },

        // runners
        test_validate_runners_empty => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![RunnerDecl{
                    name: None,
                    source: None,
                    source_path: None,
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::missing_field("RunnerDecl", "source"),
                Error::missing_field("RunnerDecl", "name"),
                Error::missing_field("RunnerDecl", "source_path"),
            ])),
        },
        test_validate_runners_invalid_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![RunnerDecl{
                    name: Some("^bad".to_string()),
                    source: Some(Ref::Collection(CollectionRef {
                        name: "/bad".to_string()
                    })),
                    source_path: Some("&bad".to_string()),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("RunnerDecl", "source"),
                Error::invalid_character_in_field("RunnerDecl", "name", '^'),
                Error::invalid_field("RunnerDecl", "source_path"),
            ])),
        },
        test_validate_runners_invalid_child => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![RunnerDecl{
                    name: Some("elf".to_string()),
                    source: Some(Ref::Collection(CollectionRef {
                        name: "invalid".to_string(),
                    })),
                    source_path: Some("/elf".to_string()),
                }]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::invalid_field("RunnerDecl", "source"),
            ])),
        },
        test_validate_runners_long_identifiers => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![
                    RunnerDecl{
                        name: Some("a".repeat(101)),
                        source: Some(Ref::Child(ChildRef {
                            name: "b".repeat(101),
                            collection: None,
                        })),
                        source_path: Some(format!("/{}", "c".repeat(1024))),
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::field_too_long("RunnerDecl", "source.child.name"),
                Error::field_too_long("RunnerDecl", "name"),
                Error::field_too_long("RunnerDecl", "source_path"),
            ])),
        },
        test_validate_runners_duplicate_name => {
            input = {
                let mut decl = new_component_decl();
                decl.runners = Some(vec![
                    RunnerDecl {
                        name: Some("elf".to_string()),
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/elf".to_string()),
                    },
                    RunnerDecl {
                        name: Some("elf".to_string()),
                        source: Some(Ref::Self_(SelfRef{})),
                        source_path: Some("/elf2".to_string()),
                    },
                ]);
                decl
            },
            result = Err(ErrorList::new(vec![
                Error::duplicate_field("RunnerDecl", "name", "elf"),
            ])),
        },
    }
}
