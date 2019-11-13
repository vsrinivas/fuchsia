// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_fidl_validator,
    failure::Fail,
    fidl_fuchsia_data as fdata, fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys,
    std::collections::HashMap,
    std::convert::{From, TryFrom, TryInto},
    std::fmt,
};

pub mod data;

/// Converts a fidl object into its corresponding native representation.
pub trait FidlIntoNative<T> {
    fn fidl_into_native(self) -> T;
}

pub trait NativeIntoFidl<T> {
    fn native_into_fidl(self) -> T;
}

/// Generates `FidlIntoNative` and `NativeIntoFidl` implementations for a basic type from
/// `Option<type>` that respectively unwraps the Option and wraps the internal value in a `Some()`.
macro_rules! fidl_translations_opt_type {
    ($into_type:ty) => {
        impl FidlIntoNative<$into_type> for Option<$into_type> {
            fn fidl_into_native(self) -> $into_type {
                self.unwrap()
            }
        }
        impl NativeIntoFidl<Option<$into_type>> for $into_type {
            fn native_into_fidl(self) -> Option<$into_type> {
                Some(self)
            }
        }
    };
}

/// Generates `FidlIntoNative` and `NativeIntoFidl` implementations that leaves the input unchanged.
macro_rules! fidl_translations_identical {
    ($into_type:ty) => {
        impl FidlIntoNative<$into_type> for $into_type {
            fn fidl_into_native(self) -> $into_type {
                self
            }
        }
        impl NativeIntoFidl<$into_type> for $into_type {
            fn native_into_fidl(self) -> Self {
                self
            }
        }
    };
}

/// Generates a struct with a `FidlIntoNative` implementation that calls `fidl_into_native()` on
/// each field.
/// - `into_type` is the name of the struct and the into type for the conversion.
/// - `into_ident` must be identical to `into_type`.
/// - `from_type` is the from type for the conversion.
/// - `from_ident` must be identical to `from_type`.
/// - `field: type` form a list of fields and their types for the generated struct.
macro_rules! fidl_into_struct {
    ($into_type:ty, $into_ident:ident, $from_type:ty, $from_path:path,
     { $( $field:ident: $type:ty, )+ } ) => {
        #[derive(Debug, Clone, PartialEq)]
        pub struct $into_ident {
            $(
                pub $field: $type,
            )+
        }

        impl FidlIntoNative<$into_type> for $from_type {
            fn fidl_into_native(self) -> $into_type {
                $into_ident { $( $field: self.$field.fidl_into_native(), )+ }
            }
        }

        impl NativeIntoFidl<$from_type> for $into_type {
            fn native_into_fidl(self) -> $from_type {
                use $from_path as from_ident;
                from_ident { $( $field: self.$field.native_into_fidl(), )+ }
            }
        }
    }
}

/// Generates an enum with a `FidlIntoNative` implementation that calls `fidl_into_native()` on each
/// field.
/// - `into_type` is the name of the enum and the into type for the conversion.
/// - `into_ident` must be identical to `into_type`.
/// - `from_type` is the from type for the conversion.
/// - `from_ident` must be identical to `from_type`.
/// - `variant(type)` form a list of variants and their types for the generated enum.
macro_rules! fidl_into_enum {
    ($into_type:ty, $into_ident:ident, $from_type:ty, $from_path:path,
     { $( $variant:ident($type:ty), )+ } ) => {
        #[derive(Debug, Clone, PartialEq)]
        pub enum $into_ident {
            $(
                $variant($type),
            )+
        }

        impl FidlIntoNative<$into_type> for $from_type {
            fn fidl_into_native(self) -> $into_type {
                use $from_path as from_ident;
                match self {
                    $(
                    from_ident::$variant(e) => $into_ident::$variant(e.fidl_into_native()),
                    )+
                    from_ident::__UnknownVariant {..} => { panic!("invalid variant") }
                }
            }
        }

        impl NativeIntoFidl<$from_type> for $into_type {
            fn native_into_fidl(self) -> $from_type {
                use $from_path as from_ident;
                match self {
                    $(
                        $into_ident::$variant(e) => from_ident::$variant(e.native_into_fidl()),
                    )+
                }
            }
        }
    }
}

/// Generates `FidlIntoNative` and `NativeIntoFidl` implementations between `Vec` and
/// `Option<Vec>`.
/// - `into_type` is the name of the struct and the into type for the conversion.
/// - `from_type` is the from type for the conversion.
macro_rules! fidl_into_vec {
    ($into_type:ty, $from_type:ty) => {
        impl FidlIntoNative<Vec<$into_type>> for Option<Vec<$from_type>> {
            fn fidl_into_native(self) -> Vec<$into_type> {
                if let Some(from) = self {
                    from.into_iter().map(|e: $from_type| e.fidl_into_native()).collect()
                } else {
                    vec![]
                }
            }
        }

        impl NativeIntoFidl<Option<Vec<$from_type>>> for Vec<$into_type> {
            fn native_into_fidl(self) -> Option<Vec<$from_type>> {
                if self.is_empty() {
                    None
                } else {
                    Some(self.into_iter().map(|e: $into_type| e.native_into_fidl()).collect())
                }
            }
        }
    };
}

#[derive(Debug, PartialEq)]
pub struct ComponentDecl {
    pub program: Option<fdata::Dictionary>,
    pub uses: Vec<UseDecl>,
    pub exposes: Vec<ExposeDecl>,
    pub offers: Vec<OfferDecl>,
    pub children: Vec<ChildDecl>,
    pub collections: Vec<CollectionDecl>,
    pub storage: Vec<StorageDecl>,
    pub facets: Option<fdata::Dictionary>,
    pub runners: Vec<RunnerDecl>,
}

impl FidlIntoNative<ComponentDecl> for fsys::ComponentDecl {
    fn fidl_into_native(self) -> ComponentDecl {
        // When transforming ExposeDecl::Service and OfferDecl::Service from
        // FIDL to native, we aggregate the declarations by target.
        let mut exposes = vec![];
        if let Some(e) = self.exposes {
            let mut services: HashMap<(ExposeTarget, CapabilityPath), Vec<_>> = HashMap::new();
            for expose in e.into_iter() {
                match expose {
                    fsys::ExposeDecl::Service(s) => services
                        .entry((s.target.fidl_into_native(), s.target_path.fidl_into_native()))
                        .or_default()
                        .push(ServiceSource::<ExposeSource> {
                            source: s.source.fidl_into_native(),
                            source_path: s.source_path.fidl_into_native(),
                        }),
                    fsys::ExposeDecl::LegacyService(ls) => {
                        exposes.push(ExposeDecl::LegacyService(ls.fidl_into_native()))
                    }
                    fsys::ExposeDecl::Directory(d) => {
                        exposes.push(ExposeDecl::Directory(d.fidl_into_native()))
                    }
                    fsys::ExposeDecl::Runner(r) => {
                        exposes.push(ExposeDecl::Runner(r.fidl_into_native()))
                    }
                    fsys::ExposeDecl::__UnknownVariant { .. } => panic!("invalid variant"),
                }
            }
            for ((target, target_path), sources) in services.into_iter() {
                exposes.push(ExposeDecl::Service(ExposeServiceDecl {
                    sources,
                    target,
                    target_path,
                }))
            }
        }
        let mut offers = vec![];
        if let Some(o) = self.offers {
            let mut services: HashMap<(OfferTarget, CapabilityPath), Vec<_>> = HashMap::new();
            for offer in o.into_iter() {
                match offer {
                    fsys::OfferDecl::Service(s) => services
                        .entry((s.target.fidl_into_native(), s.target_path.fidl_into_native()))
                        .or_default()
                        .push(ServiceSource::<OfferServiceSource> {
                            source: s.source.fidl_into_native(),
                            source_path: s.source_path.fidl_into_native(),
                        }),
                    fsys::OfferDecl::LegacyService(ls) => {
                        offers.push(OfferDecl::LegacyService(ls.fidl_into_native()))
                    }
                    fsys::OfferDecl::Directory(d) => {
                        offers.push(OfferDecl::Directory(d.fidl_into_native()))
                    }
                    fsys::OfferDecl::Storage(s) => {
                        offers.push(OfferDecl::Storage(s.fidl_into_native()))
                    }
                    fsys::OfferDecl::Runner(s) => {
                        offers.push(OfferDecl::Runner(s.fidl_into_native()))
                    }
                    fsys::OfferDecl::__UnknownVariant { .. } => panic!("invalid variant"),
                }
            }
            for ((target, target_path), sources) in services.into_iter() {
                offers.push(OfferDecl::Service(OfferServiceDecl { sources, target, target_path }))
            }
        }
        ComponentDecl {
            program: self.program.fidl_into_native(),
            uses: self.uses.fidl_into_native(),
            exposes,
            offers,
            children: self.children.fidl_into_native(),
            collections: self.collections.fidl_into_native(),
            storage: self.storage.fidl_into_native(),
            facets: self.facets.fidl_into_native(),
            runners: self.runners.fidl_into_native(),
        }
    }
}

impl NativeIntoFidl<fsys::ComponentDecl> for ComponentDecl {
    fn native_into_fidl(self) -> fsys::ComponentDecl {
        // When transforming ExposeDecl::Service and OfferDecl::Service from
        // native to FIDL, we disaggregate the declarations.
        let mut exposes = vec![];
        for expose in self.exposes.into_iter() {
            match expose {
                ExposeDecl::Service(s) => {
                    for es in s.sources.into_iter() {
                        exposes.push(fsys::ExposeDecl::Service(fsys::ExposeServiceDecl {
                            source: es.source.native_into_fidl(),
                            source_path: es.source_path.native_into_fidl(),
                            target: s.target.clone().native_into_fidl(),
                            target_path: s.target_path.clone().native_into_fidl(),
                        }))
                    }
                }
                ExposeDecl::LegacyService(ls) => {
                    exposes.push(fsys::ExposeDecl::LegacyService(ls.native_into_fidl()))
                }
                ExposeDecl::Directory(d) => {
                    exposes.push(fsys::ExposeDecl::Directory(d.native_into_fidl()))
                }
                ExposeDecl::Runner(r) => {
                    exposes.push(fsys::ExposeDecl::Runner(r.native_into_fidl()))
                }
            }
        }
        let mut offers = vec![];
        for offer in self.offers.into_iter() {
            match offer {
                OfferDecl::Service(s) => {
                    for os in s.sources.into_iter() {
                        offers.push(fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                            source: os.source.native_into_fidl(),
                            source_path: os.source_path.native_into_fidl(),
                            target: s.target.clone().native_into_fidl(),
                            target_path: s.target_path.clone().native_into_fidl(),
                        }))
                    }
                }
                OfferDecl::LegacyService(ls) => {
                    offers.push(fsys::OfferDecl::LegacyService(ls.native_into_fidl()))
                }
                OfferDecl::Directory(d) => {
                    offers.push(fsys::OfferDecl::Directory(d.native_into_fidl()))
                }
                OfferDecl::Storage(s) => {
                    offers.push(fsys::OfferDecl::Storage(s.native_into_fidl()))
                }
                OfferDecl::Runner(s) => offers.push(fsys::OfferDecl::Runner(s.native_into_fidl())),
            }
        }
        fsys::ComponentDecl {
            program: self.program.native_into_fidl(),
            uses: self.uses.native_into_fidl(),
            exposes: if exposes.is_empty() { None } else { Some(exposes) },
            offers: if offers.is_empty() { None } else { Some(offers) },
            children: self.children.native_into_fidl(),
            collections: self.collections.native_into_fidl(),
            storage: self.storage.native_into_fidl(),
            facets: self.facets.native_into_fidl(),
            runners: self.runners.native_into_fidl(),
        }
    }
}

impl Clone for ComponentDecl {
    fn clone(&self) -> Self {
        ComponentDecl {
            program: data::clone_option_dictionary(&self.program),
            uses: self.uses.clone(),
            exposes: self.exposes.clone(),
            offers: self.offers.clone(),
            children: self.children.clone(),
            collections: self.collections.clone(),
            storage: self.storage.clone(),
            facets: data::clone_option_dictionary(&self.facets),
            runners: self.runners.clone(),
        }
    }
}

impl ComponentDecl {
    /// Returns the `StorageDecl` corresponding to `storage_name`.
    pub fn find_storage_source<'a>(&'a self, storage_name: &str) -> Option<&'a StorageDecl> {
        self.storage.iter().find(|s| &s.name == storage_name)
    }

    /// Returns the `CollectionDecl` corresponding to `collection_name`.
    pub fn find_collection<'a>(&'a self, collection_name: &str) -> Option<&'a CollectionDecl> {
        self.collections.iter().find(|c| c.name == collection_name)
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum ExposeDecl {
    Service(ExposeServiceDecl),
    LegacyService(ExposeLegacyServiceDecl),
    Directory(ExposeDirectoryDecl),
    Runner(ExposeRunnerDecl),
}

#[derive(Debug, Clone, PartialEq)]
pub struct ExposeServiceDecl {
    pub sources: Vec<ServiceSource<ExposeSource>>,
    pub target: ExposeTarget,
    pub target_path: CapabilityPath,
}

#[derive(Debug, Clone, PartialEq)]
pub enum OfferDecl {
    Service(OfferServiceDecl),
    LegacyService(OfferLegacyServiceDecl),
    Directory(OfferDirectoryDecl),
    Storage(OfferStorageDecl),
    Runner(OfferRunnerDecl),
}

#[derive(Debug, Clone, PartialEq)]
pub struct OfferServiceDecl {
    pub sources: Vec<ServiceSource<OfferServiceSource>>,
    pub target: OfferTarget,
    pub target_path: CapabilityPath,
}

fidl_into_enum!(UseDecl, UseDecl, fsys::UseDecl, fsys::UseDecl,
                {
                    Service(UseServiceDecl),
                    LegacyService(UseLegacyServiceDecl),
                    Directory(UseDirectoryDecl),
                    Storage(UseStorageDecl),
                    Runner(UseRunnerDecl),
                });
fidl_into_struct!(UseServiceDecl, UseServiceDecl, fsys::UseServiceDecl, fsys::UseServiceDecl,
                  {
                      source: UseSource,
                      source_path: CapabilityPath,
                      target_path: CapabilityPath,
                  });
fidl_into_struct!(UseLegacyServiceDecl, UseLegacyServiceDecl, fsys::UseLegacyServiceDecl, fsys::UseLegacyServiceDecl,
                  {
                      source: UseSource,
                      source_path: CapabilityPath,
                      target_path: CapabilityPath,
                  });
fidl_into_struct!(UseDirectoryDecl, UseDirectoryDecl, fsys::UseDirectoryDecl,
                  fsys::UseDirectoryDecl,
                  {
                      source: UseSource,
                      source_path: CapabilityPath,
                      target_path: CapabilityPath,
                      rights: fio2::Operations,
                  });
fidl_into_struct!(UseRunnerDecl, UseRunnerDecl, fsys::UseRunnerDecl,
                  fsys::UseRunnerDecl,
                  {
                      source_name: CapabilityName,
                  });

fidl_into_struct!(ExposeLegacyServiceDecl, ExposeLegacyServiceDecl, fsys::ExposeLegacyServiceDecl,
                  fsys::ExposeLegacyServiceDecl,
                  {
                      source: ExposeSource,
                      source_path: CapabilityPath,
                      target: ExposeTarget,
                      target_path: CapabilityPath,
                  });
fidl_into_struct!(ExposeDirectoryDecl, ExposeDirectoryDecl, fsys::ExposeDirectoryDecl,
                  fsys::ExposeDirectoryDecl,
                  {
                      source: ExposeSource,
                      source_path: CapabilityPath,
                      target: ExposeTarget,
                      target_path: CapabilityPath,
                      rights: Option<fio2::Operations>,
                  });
fidl_into_struct!(ExposeRunnerDecl, ExposeRunnerDecl, fsys::ExposeRunnerDecl,
                  fsys::ExposeRunnerDecl,
                  {
                      source: ExposeSource,
                      source_name: CapabilityName,
                      target: ExposeTarget,
                      target_name: CapabilityName,
                  });

fidl_into_struct!(StorageDecl, StorageDecl, fsys::StorageDecl,
                  fsys::StorageDecl,
                  {
                      name: String,
                      source: StorageDirectorySource,
                      source_path: CapabilityPath,
                  });
fidl_into_struct!(OfferLegacyServiceDecl, OfferLegacyServiceDecl, fsys::OfferLegacyServiceDecl,
                  fsys::OfferLegacyServiceDecl,
                  {
                      source: OfferServiceSource,
                      source_path: CapabilityPath,
                      target: OfferTarget,
                      target_path: CapabilityPath,
                  });
fidl_into_struct!(OfferDirectoryDecl, OfferDirectoryDecl, fsys::OfferDirectoryDecl,
                  fsys::OfferDirectoryDecl,
                  {
                      source: OfferDirectorySource,
                      source_path: CapabilityPath,
                      target: OfferTarget,
                      target_path: CapabilityPath,
                      rights: Option<fio2::Operations>,
                  });
fidl_into_struct!(OfferRunnerDecl, OfferRunnerDecl, fsys::OfferRunnerDecl,
                  fsys::OfferRunnerDecl,
                  {
                      source: OfferRunnerSource,
                      source_name: CapabilityName,
                      target: OfferTarget,
                      target_name: CapabilityName,
                  });

fidl_into_struct!(ChildDecl, ChildDecl, fsys::ChildDecl, fsys::ChildDecl,
                  {
                      name: String,
                      url: String,
                      startup: fsys::StartupMode,
                  });

fidl_into_struct!(CollectionDecl, CollectionDecl, fsys::CollectionDecl, fsys::CollectionDecl,
                  {
                      name: String,
                      durability: fsys::Durability,
                  });
fidl_into_struct!(RunnerDecl, RunnerDecl, fsys::RunnerDecl, fsys::RunnerDecl,
                  {
                      name: String,
                      source: RunnerSource,
                      source_path: CapabilityPath,
                  });

fidl_into_vec!(UseDecl, fsys::UseDecl);
fidl_into_vec!(ChildDecl, fsys::ChildDecl);
fidl_into_vec!(CollectionDecl, fsys::CollectionDecl);
fidl_into_vec!(StorageDecl, fsys::StorageDecl);
fidl_into_vec!(RunnerDecl, fsys::RunnerDecl);
fidl_translations_opt_type!(String);
fidl_translations_opt_type!(fsys::StartupMode);
fidl_translations_opt_type!(fsys::Durability);
fidl_translations_opt_type!(fdata::Dictionary);
fidl_translations_opt_type!(fio2::Operations);
fidl_translations_identical!(Option<fio2::Operations>);
fidl_translations_identical!(Option<fdata::Dictionary>);

/// A path to a capability.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct CapabilityPath {
    /// The directory containing the last path element, e.g. `/svc/foo` in `/svc/foo/bar`.
    pub dirname: String,
    /// The last path element: e.g. `bar` in `/svc/foo/bar`.
    pub basename: String,
}

impl CapabilityPath {
    pub fn to_string(&self) -> String {
        format!("{}", self)
    }

    /// Splits the path according to "/", ignoring empty path components
    pub fn split(&self) -> Vec<String> {
        self.to_string().split("/").map(|s| s.to_string()).filter(|s| !s.is_empty()).collect()
    }
}

impl TryFrom<&str> for CapabilityPath {
    type Error = Error;
    fn try_from(path: &str) -> Result<CapabilityPath, Error> {
        if !path.starts_with("/") {
            // Paths must be absolute.
            return Err(Error::InvalidCapabilityPath { raw: path.to_string() });
        }
        let mut parts = vec![];
        let mut parts_iter = path.split("/");
        // Skip empty component generated by first '/'.
        parts_iter.next();
        for part in parts_iter {
            if part.is_empty() {
                // Paths may not have empty components.
                return Err(Error::InvalidCapabilityPath { raw: path.to_string() });
            }
            parts.push(part);
        }
        if parts.is_empty() {
            // Root paths are not allowed.
            return Err(Error::InvalidCapabilityPath { raw: path.to_string() });
        }
        Ok(CapabilityPath {
            dirname: format!("/{}", parts[..parts.len() - 1].join("/")),
            basename: parts.last().unwrap().to_string(),
        })
    }
}

impl fmt::Display for CapabilityPath {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if &self.dirname == "/" {
            write!(f, "/{}", self.basename)
        } else {
            write!(f, "{}/{}", self.dirname, self.basename)
        }
    }
}

impl UseDecl {
    pub fn path(&self) -> Option<&CapabilityPath> {
        let path = match self {
            UseDecl::Service(d) => &d.target_path,
            UseDecl::LegacyService(d) => &d.target_path,
            UseDecl::Directory(d) => &d.target_path,
            UseDecl::Storage(UseStorageDecl::Data(p)) => &p,
            UseDecl::Storage(UseStorageDecl::Cache(p)) => &p,
            UseDecl::Storage(UseStorageDecl::Meta) | UseDecl::Runner(_) => {
                // Meta storage and runners don't show up in the namespace; no capability path.
                return None;
            }
        };
        Some(path)
    }
}

/// A named capability.
///
/// Unlike a `CapabilityPath`, a `CapabilityName` doesn't encode any form
/// of hierarchy.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct CapabilityName(pub String);

impl CapabilityName {
    pub fn to_string(&self) -> String {
        self.0.to_string()
    }
}

impl From<&str> for CapabilityName {
    fn from(name: &str) -> CapabilityName {
        CapabilityName(name.to_string())
    }
}

impl From<&CapabilityName> for CapabilityName {
    fn from(name: &CapabilityName) -> CapabilityName {
        name.clone()
    }
}

impl fmt::Display for CapabilityName {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(&self.0)
    }
}

// TODO: Runners and third parties can use this to parse `program` or `facets`.
impl FidlIntoNative<Option<HashMap<String, Value>>> for Option<fdata::Dictionary> {
    fn fidl_into_native(self) -> Option<HashMap<String, Value>> {
        self.map(|d| from_fidl_dict(d))
    }
}

impl FidlIntoNative<CapabilityPath> for Option<String> {
    fn fidl_into_native(self) -> CapabilityPath {
        let s: &str = &self.unwrap();
        s.try_into().expect("invalid capability path")
    }
}

impl NativeIntoFidl<Option<String>> for CapabilityPath {
    fn native_into_fidl(self) -> Option<String> {
        Some(self.to_string())
    }
}

impl FidlIntoNative<CapabilityName> for Option<String> {
    fn fidl_into_native(self) -> CapabilityName {
        let s: &str = &self.unwrap();
        s.into()
    }
}

impl NativeIntoFidl<Option<String>> for CapabilityName {
    fn native_into_fidl(self) -> Option<String> {
        Some(self.to_string())
    }
}

#[derive(Debug, PartialEq)]
pub enum Value {
    Bit(bool),
    Inum(i64),
    Fnum(f64),
    Str(String),
    Vec(Vec<Value>),
    Dict(HashMap<String, Value>),
    Null,
}

impl FidlIntoNative<Value> for Option<Box<fdata::Value>> {
    fn fidl_into_native(self) -> Value {
        match self {
            Some(v) => match *v {
                fdata::Value::Bit(b) => Value::Bit(b),
                fdata::Value::Inum(i) => Value::Inum(i),
                fdata::Value::Fnum(f) => Value::Fnum(f),
                fdata::Value::Str(s) => Value::Str(s),
                fdata::Value::Vec(v) => Value::Vec(from_fidl_vec(v)),
                fdata::Value::Dict(d) => Value::Dict(from_fidl_dict(d)),
            },
            None => Value::Null,
        }
    }
}

fn from_fidl_vec(vec: fdata::Vector) -> Vec<Value> {
    vec.values.into_iter().map(|v| v.fidl_into_native()).collect()
}

fn from_fidl_dict(dict: fdata::Dictionary) -> HashMap<String, Value> {
    dict.entries.into_iter().map(|e| (e.key, e.value.fidl_into_native())).collect()
}

#[derive(Debug, Clone, PartialEq)]
pub enum UseStorageDecl {
    Data(CapabilityPath),
    Cache(CapabilityPath),
    Meta,
}

impl FidlIntoNative<UseStorageDecl> for fsys::UseStorageDecl {
    fn fidl_into_native(self) -> UseStorageDecl {
        match self.type_.unwrap() {
            fsys::StorageType::Data => {
                UseStorageDecl::Data(self.target_path.unwrap().as_str().try_into().unwrap())
            }
            fsys::StorageType::Cache => {
                UseStorageDecl::Cache(self.target_path.unwrap().as_str().try_into().unwrap())
            }
            fsys::StorageType::Meta => UseStorageDecl::Meta,
        }
    }
}

impl NativeIntoFidl<fsys::UseStorageDecl> for UseStorageDecl {
    fn native_into_fidl(self) -> fsys::UseStorageDecl {
        match self {
            UseStorageDecl::Data(p) => fsys::UseStorageDecl {
                type_: Some(fsys::StorageType::Data),
                target_path: p.native_into_fidl(),
            },
            UseStorageDecl::Cache(p) => fsys::UseStorageDecl {
                type_: Some(fsys::StorageType::Cache),
                target_path: p.native_into_fidl(),
            },
            UseStorageDecl::Meta => {
                fsys::UseStorageDecl { type_: Some(fsys::StorageType::Meta), target_path: None }
            }
        }
    }
}

impl UseStorageDecl {
    pub fn type_(&self) -> fsys::StorageType {
        match self {
            UseStorageDecl::Data(_) => fsys::StorageType::Data,
            UseStorageDecl::Cache(_) => fsys::StorageType::Cache,
            UseStorageDecl::Meta => fsys::StorageType::Meta,
        }
    }

    pub fn path<'a>(&'a self) -> Option<&'a CapabilityPath> {
        match self {
            UseStorageDecl::Data(p) => Some(p),
            UseStorageDecl::Cache(p) => Some(p),
            UseStorageDecl::Meta => None,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum OfferStorageDecl {
    Data(OfferStorage),
    Cache(OfferStorage),
    Meta(OfferStorage),
}

#[derive(Debug, Clone, PartialEq)]
pub struct OfferStorage {
    pub source: OfferStorageSource,
    pub target: OfferTarget,
}

impl FidlIntoNative<OfferStorageDecl> for fsys::OfferStorageDecl {
    fn fidl_into_native(self) -> OfferStorageDecl {
        match self.type_.unwrap() {
            fsys::StorageType::Data => OfferStorageDecl::Data(OfferStorage {
                source: self.source.fidl_into_native(),
                target: self.target.fidl_into_native(),
            }),
            fsys::StorageType::Cache => OfferStorageDecl::Cache(OfferStorage {
                source: self.source.fidl_into_native(),
                target: self.target.fidl_into_native(),
            }),
            fsys::StorageType::Meta => OfferStorageDecl::Meta(OfferStorage {
                source: self.source.fidl_into_native(),
                target: self.target.fidl_into_native(),
            }),
        }
    }
}

impl NativeIntoFidl<fsys::OfferStorageDecl> for OfferStorageDecl {
    fn native_into_fidl(self) -> fsys::OfferStorageDecl {
        let type_ = self.type_();
        let (source, target) = match self {
            OfferStorageDecl::Data(OfferStorage { source, target }) => (source, target),
            OfferStorageDecl::Cache(OfferStorage { source, target }) => (source, target),
            OfferStorageDecl::Meta(OfferStorage { source, target }) => (source, target),
        };
        fsys::OfferStorageDecl {
            type_: Some(type_),
            source: source.native_into_fidl(),
            target: target.native_into_fidl(),
        }
    }
}

impl OfferStorageDecl {
    pub fn type_(&self) -> fsys::StorageType {
        match self {
            OfferStorageDecl::Data(..) => fsys::StorageType::Data,
            OfferStorageDecl::Cache(..) => fsys::StorageType::Cache,
            OfferStorageDecl::Meta(..) => fsys::StorageType::Meta,
        }
    }

    pub fn source(&self) -> &OfferStorageSource {
        match self {
            OfferStorageDecl::Data(OfferStorage { source, .. }) => source,
            OfferStorageDecl::Cache(OfferStorage { source, .. }) => source,
            OfferStorageDecl::Meta(OfferStorage { source, .. }) => source,
        }
    }

    pub fn target(&self) -> &OfferTarget {
        match self {
            OfferStorageDecl::Data(OfferStorage { target, .. }) => target,
            OfferStorageDecl::Cache(OfferStorage { target, .. }) => target,
            OfferStorageDecl::Meta(OfferStorage { target, .. }) => target,
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum UseSource {
    Realm,
    Framework,
}

impl FidlIntoNative<UseSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> UseSource {
        match self.unwrap() {
            fsys::Ref::Realm(_) => UseSource::Realm,
            fsys::Ref::Framework(_) => UseSource::Framework,
            _ => panic!("invalid UseSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for UseSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            UseSource::Realm => fsys::Ref::Realm(fsys::RealmRef {}),
            UseSource::Framework => fsys::Ref::Framework(fsys::FrameworkRef {}),
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum ExposeSource {
    Self_,
    Child(String),
    Framework,
}

impl FidlIntoNative<ExposeSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> ExposeSource {
        match self.unwrap() {
            fsys::Ref::Self_(_) => ExposeSource::Self_,
            fsys::Ref::Child(c) => ExposeSource::Child(c.name),
            fsys::Ref::Framework(_) => ExposeSource::Framework,
            _ => panic!("invalid ExposeSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for ExposeSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            ExposeSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            ExposeSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
            ExposeSource::Framework => fsys::Ref::Framework(fsys::FrameworkRef {}),
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum ExposeTarget {
    Realm,
    Framework,
}

impl FidlIntoNative<ExposeTarget> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> ExposeTarget {
        match self {
            Some(dest) => match dest {
                fsys::Ref::Realm(_) => ExposeTarget::Realm,
                fsys::Ref::Framework(_) => ExposeTarget::Framework,
                _ => panic!("invalid ExposeTarget variant"),
            },
            None => ExposeTarget::Realm,
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for ExposeTarget {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            ExposeTarget::Realm => fsys::Ref::Realm(fsys::RealmRef {}),
            ExposeTarget::Framework => fsys::Ref::Framework(fsys::FrameworkRef {}),
        })
    }
}

/// A source for a service.
#[derive(Debug, Clone, PartialEq)]
pub struct ServiceSource<T> {
    /// The provider of the service, relative to a component.
    pub source: T,
    /// The path at which the service is accessible.
    pub source_path: CapabilityPath,
}

#[derive(Debug, Clone, PartialEq)]
pub enum OfferServiceSource {
    Realm,
    Self_,
    Child(String),
}

impl FidlIntoNative<OfferServiceSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferServiceSource {
        match self.unwrap() {
            fsys::Ref::Realm(_) => OfferServiceSource::Realm,
            fsys::Ref::Self_(_) => OfferServiceSource::Self_,
            fsys::Ref::Child(c) => OfferServiceSource::Child(c.name),
            _ => panic!("invalid OfferServiceSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for OfferServiceSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            OfferServiceSource::Realm => fsys::Ref::Realm(fsys::RealmRef {}),
            OfferServiceSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            OfferServiceSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum StorageDirectorySource {
    Realm,
    Self_,
    Child(String),
}

impl FidlIntoNative<StorageDirectorySource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> StorageDirectorySource {
        match self.unwrap() {
            fsys::Ref::Realm(_) => StorageDirectorySource::Realm,
            fsys::Ref::Self_(_) => StorageDirectorySource::Self_,
            fsys::Ref::Child(c) => StorageDirectorySource::Child(c.name),
            _ => panic!("invalid OfferDirectorySource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for StorageDirectorySource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            StorageDirectorySource::Realm => fsys::Ref::Realm(fsys::RealmRef {}),
            StorageDirectorySource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            StorageDirectorySource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum RunnerSource {
    Realm,
    Self_,
    Child(String),
}

impl FidlIntoNative<RunnerSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> RunnerSource {
        match self.unwrap() {
            fsys::Ref::Realm(_) => RunnerSource::Realm,
            fsys::Ref::Self_(_) => RunnerSource::Self_,
            fsys::Ref::Child(c) => RunnerSource::Child(c.name),
            _ => panic!("invalid RunnerSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for RunnerSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            RunnerSource::Realm => fsys::Ref::Realm(fsys::RealmRef {}),
            RunnerSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            RunnerSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum OfferDirectorySource {
    Realm,
    Self_,
    Framework,
    Child(String),
}

impl FidlIntoNative<OfferDirectorySource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferDirectorySource {
        match self.unwrap() {
            fsys::Ref::Realm(_) => OfferDirectorySource::Realm,
            fsys::Ref::Self_(_) => OfferDirectorySource::Self_,
            fsys::Ref::Framework(_) => OfferDirectorySource::Framework,
            fsys::Ref::Child(c) => OfferDirectorySource::Child(c.name),
            _ => panic!("invalid OfferDirectorySource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for OfferDirectorySource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            OfferDirectorySource::Realm => fsys::Ref::Realm(fsys::RealmRef {}),
            OfferDirectorySource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            OfferDirectorySource::Framework => fsys::Ref::Framework(fsys::FrameworkRef {}),
            OfferDirectorySource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum OfferStorageSource {
    Realm,
    Storage(String),
}

impl FidlIntoNative<OfferStorageSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferStorageSource {
        match self.unwrap() {
            fsys::Ref::Realm(_) => OfferStorageSource::Realm,
            fsys::Ref::Storage(c) => OfferStorageSource::Storage(c.name),
            _ => panic!("invalid OfferStorageSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for OfferStorageSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            OfferStorageSource::Realm => fsys::Ref::Realm(fsys::RealmRef {}),
            OfferStorageSource::Storage(name) => fsys::Ref::Storage(fsys::StorageRef { name }),
        })
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum OfferRunnerSource {
    Realm,
    Self_,
    Child(String),
}

impl FidlIntoNative<OfferRunnerSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferRunnerSource {
        match self.unwrap() {
            fsys::Ref::Realm(_) => OfferRunnerSource::Realm,
            fsys::Ref::Self_(_) => OfferRunnerSource::Self_,
            fsys::Ref::Child(c) => OfferRunnerSource::Child(c.name),
            _ => panic!("invalid OfferRunnerSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for OfferRunnerSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            OfferRunnerSource::Realm => fsys::Ref::Realm(fsys::RealmRef {}),
            OfferRunnerSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            OfferRunnerSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum OfferTarget {
    Child(String),
    Collection(String),
}

impl FidlIntoNative<OfferTarget> for fsys::Ref {
    fn fidl_into_native(self) -> OfferTarget {
        match self {
            fsys::Ref::Child(c) => OfferTarget::Child(c.name),
            fsys::Ref::Collection(c) => OfferTarget::Collection(c.name),
            _ => panic!("invalid OfferTarget variant"),
        }
    }
}

impl NativeIntoFidl<fsys::Ref> for OfferTarget {
    fn native_into_fidl(self) -> fsys::Ref {
        match self {
            OfferTarget::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
            OfferTarget::Collection(collection_name) => {
                fsys::Ref::Collection(fsys::CollectionRef { name: collection_name })
            }
        }
    }
}

impl FidlIntoNative<OfferTarget> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferTarget {
        self.unwrap().fidl_into_native()
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for OfferTarget {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(self.native_into_fidl())
    }
}

/// Converts the contents of a CM-FIDL declaration and produces the equivalent CM-Rust
/// struct.
/// This function applies cm_fidl_validator to check correctness.
impl TryFrom<fsys::ComponentDecl> for ComponentDecl {
    type Error = Error;

    fn try_from(decl: fsys::ComponentDecl) -> Result<Self, Self::Error> {
        cm_fidl_validator::validate(&decl).map_err(|err| Error::Validate { err })?;
        Ok(decl.fidl_into_native())
    }
}

// Converts the contents of a CM-Rust declaration into a CM_FIDL declaration
impl TryFrom<ComponentDecl> for fsys::ComponentDecl {
    type Error = Error;
    fn try_from(decl: ComponentDecl) -> Result<Self, Self::Error> {
        Ok(decl.native_into_fidl())
    }
}

/// Errors produced by cm_rust.
#[derive(Debug, Fail)]
pub enum Error {
    #[fail(display = "Fidl validation failed: {}", err)]
    Validate {
        #[fail(cause)]
        err: cm_fidl_validator::ErrorList,
    },
    #[fail(display = "Invalid capability path: {}", raw)]
    InvalidCapabilityPath { raw: String },
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! test_try_from_decl {
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
                    {
                        let res = ComponentDecl::try_from($input).expect("try_from failed");
                        assert_eq!(res, $result);
                    }
                    {
                        let res = fsys::ComponentDecl::try_from($result).expect("try_from failed");
                        assert_eq!(res, $input);
                    }
                }
            )+
        }
    }

    macro_rules! test_fidl_into_and_from {
        (
            $(
                $test_name:ident => {
                    input = $input:expr,
                    input_type = $input_type:ty,
                    result = $result:expr,
                    result_type = $result_type:ty,
                },
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    {
                        let res: Vec<$result_type> =
                            $input.into_iter().map(|e| e.fidl_into_native()).collect();
                        assert_eq!(res, $result);
                    }
                    {
                        let res: Vec<$input_type> =
                            $result.into_iter().map(|e| e.native_into_fidl()).collect();
                        assert_eq!(res, $input);
                    }
                }
            )+
        }
    }

    macro_rules! test_fidl_into {
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
                    test_fidl_into_helper($input, $result);
                }
            )+
        }
    }

    fn test_fidl_into_helper<T, U>(input: T, expected_res: U)
    where
        T: FidlIntoNative<U>,
        U: std::cmp::PartialEq + std::fmt::Debug,
    {
        let res: U = input.fidl_into_native();
        assert_eq!(res, expected_res);
    }

    macro_rules! test_capability_path {
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
                    test_capability_path_helper($input, $result);
                }
            )+
        }
    }

    fn test_capability_path_helper(input: &str, result: Result<CapabilityPath, Error>) {
        let res = CapabilityPath::try_from(input);
        assert_eq!(format!("{:?}", res), format!("{:?}", result));
        if let Ok(p) = res {
            assert_eq!(&p.to_string(), input);
        }
    }

    test_try_from_decl! {
        try_from_empty => {
            input = fsys::ComponentDecl {
                program: None,
                uses: None,
                exposes: None,
                offers: None,
                children: None,
                collections: None,
                facets: None,
                storage: None,
                runners: None,
            },
            result = ComponentDecl {
                program: None,
                uses: vec![],
                exposes: vec![],
                offers: vec![],
                children: vec![],
                collections: vec![],
                storage: vec![],
                facets: None,
                runners: vec![],
            },
        },
        try_from_all => {
            input = fsys::ComponentDecl {
               program: Some(fdata::Dictionary{entries: vec![
                   fdata::Entry{
                       key: "binary".to_string(),
                       value: Some(Box::new(fdata::Value::Str("bin/app".to_string()))),
                   },
               ]}),
               uses: Some(vec![
                   fsys::UseDecl::Service(fsys::UseServiceDecl {
                       source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                       source_path: Some("/svc/netstack".to_string()),
                       target_path: Some("/svc/mynetstack".to_string()),
                   }),
                   fsys::UseDecl::LegacyService(fsys::UseLegacyServiceDecl {
                       source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                       source_path: Some("/svc/legacy_netstack".to_string()),
                       target_path: Some("/svc/legacy_mynetstack".to_string()),
                   }),
                   fsys::UseDecl::Directory(fsys::UseDirectoryDecl {
                       source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                       source_path: Some("/data/dir".to_string()),
                       target_path: Some("/data".to_string()),
                       rights: Some(fio2::Operations::Connect),
                   }),
                   fsys::UseDecl::Storage(fsys::UseStorageDecl {
                       type_: Some(fsys::StorageType::Cache),
                       target_path: Some("/cache".to_string()),
                   }),
                   fsys::UseDecl::Storage(fsys::UseStorageDecl {
                       type_: Some(fsys::StorageType::Meta),
                       target_path: None,
                   }),
                   fsys::UseDecl::Runner(fsys::UseRunnerDecl {
                       source_name: Some("myrunner".to_string()),
                   }),
               ]),
               exposes: Some(vec![
                   fsys::ExposeDecl::LegacyService(fsys::ExposeLegacyServiceDecl {
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_path: Some("/svc/legacy_netstack".to_string()),
                       target_path: Some("/svc/legacy_mynetstack".to_string()),
                       target: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                   }),
                   fsys::ExposeDecl::Directory(fsys::ExposeDirectoryDecl {
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_path: Some("/data/dir".to_string()),
                       target_path: Some("/data".to_string()),
                       target: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                       rights: Some(fio2::Operations::Connect),
                   }),
                   fsys::ExposeDecl::Runner(fsys::ExposeRunnerDecl {
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_name: Some("elf".to_string()),
                       target: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                       target_name: Some("elf".to_string()),
                   }),
                   fsys::ExposeDecl::Service(fsys::ExposeServiceDecl {
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_path: Some("/svc/netstack1".to_string()),
                       target_path: Some("/svc/mynetstack".to_string()),
                       target: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                   }),
                   fsys::ExposeDecl::Service(fsys::ExposeServiceDecl {
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_path: Some("/svc/netstack2".to_string()),
                       target_path: Some("/svc/mynetstack".to_string()),
                       target: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                   }),
               ]),
               offers: Some(vec![
                   fsys::OfferDecl::LegacyService(fsys::OfferLegacyServiceDecl {
                       source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                       source_path: Some("/svc/legacy_netstack".to_string()),
                       target: Some(fsys::Ref::Child(
                          fsys::ChildRef {
                              name: "echo".to_string(),
                              collection: None,
                          }
                       )),
                       target_path: Some("/svc/legacy_mynetstack".to_string()),
                   }),
                   fsys::OfferDecl::Directory(fsys::OfferDirectoryDecl {
                       source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                       source_path: Some("/data/dir".to_string()),
                       target: Some(fsys::Ref::Collection(
                           fsys::CollectionRef { name: "modular".to_string() }
                       )),
                       target_path: Some("/data".to_string()),
                       rights: Some(fio2::Operations::Connect),
                   }),
                   fsys::OfferDecl::Storage(fsys::OfferStorageDecl {
                       type_: Some(fsys::StorageType::Cache),
                       source: Some(fsys::Ref::Storage(fsys::StorageRef {
                           name: "memfs".to_string(),
                       })),
                       target: Some(fsys::Ref::Collection(
                           fsys::CollectionRef { name: "modular".to_string() }
                       )),
                   }),
                   fsys::OfferDecl::Runner(fsys::OfferRunnerDecl {
                       source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                       source_name: Some("elf".to_string()),
                       target: Some(fsys::Ref::Child(
                          fsys::ChildRef {
                              name: "echo".to_string(),
                              collection: None,
                          }
                       )),
                       target_name: Some("elf2".to_string()),
                   }),
                   fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                       source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                       source_path: Some("/svc/netstack1".to_string()),
                       target: Some(fsys::Ref::Child(
                          fsys::ChildRef {
                              name: "echo".to_string(),
                              collection: None,
                          }
                       )),
                       target_path: Some("/svc/mynetstack".to_string()),
                   }),
                   fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                       source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                       source_path: Some("/svc/netstack2".to_string()),
                       target: Some(fsys::Ref::Child(
                          fsys::ChildRef {
                              name: "echo".to_string(),
                              collection: None,
                          }
                       )),
                       target_path: Some("/svc/mynetstack".to_string()),
                   }),
               ]),
               children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cm"
                                  .to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                    },
                    fsys::ChildDecl {
                        name: Some("echo".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/echo#meta/echo.cm"
                                  .to_string()),
                        startup: Some(fsys::StartupMode::Eager),
                    },
               ]),
               collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                    },
                    fsys::CollectionDecl {
                        name: Some("tests".to_string()),
                        durability: Some(fsys::Durability::Transient),
                    },
               ]),
               facets: Some(fdata::Dictionary{entries: vec![
                   fdata::Entry{
                       key: "author".to_string(),
                       value: Some(Box::new(fdata::Value::Str("Fuchsia".to_string()))),
                   },
               ]}),
               storage: Some(vec![
                   fsys::StorageDecl {
                       name: Some("memfs".to_string()),
                       source_path: Some("/memfs".to_string()),
                       source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                   }
               ]),
               runners: Some(vec![
                   fsys::RunnerDecl {
                       name: Some("elf".to_string()),
                       source_path: Some("/elf".to_string()),
                       source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                   }
               ]),
            },
            result = {
                ComponentDecl {
                    program: Some(fdata::Dictionary{entries: vec![
                        fdata::Entry{
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::Value::Str("bin/app".to_string()))),
                        },
                    ]}),
                    uses: vec![
                        UseDecl::Service(UseServiceDecl {
                            source: UseSource::Realm,
                            source_path: "/svc/netstack".try_into().unwrap(),
                            target_path: "/svc/mynetstack".try_into().unwrap(),
                        }),
                        UseDecl::LegacyService(UseLegacyServiceDecl {
                            source: UseSource::Realm,
                            source_path: "/svc/legacy_netstack".try_into().unwrap(),
                            target_path: "/svc/legacy_mynetstack".try_into().unwrap(),
                        }),
                        UseDecl::Directory(UseDirectoryDecl {
                            source: UseSource::Framework,
                            source_path: "/data/dir".try_into().unwrap(),
                            target_path: "/data".try_into().unwrap(),
                            rights: fio2::Operations::Connect,
                        }),
                        UseDecl::Storage(UseStorageDecl::Cache("/cache".try_into().unwrap())),
                        UseDecl::Storage(UseStorageDecl::Meta),
                        UseDecl::Runner(UseRunnerDecl {
                            source_name: "myrunner".into(),
                        }),
                    ],
                    exposes: vec![
                        ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                            source: ExposeSource::Child("netstack".to_string()),
                            source_path: "/svc/legacy_netstack".try_into().unwrap(),
                            target_path: "/svc/legacy_mynetstack".try_into().unwrap(),
                            target: ExposeTarget::Realm,
                        }),
                        ExposeDecl::Directory(ExposeDirectoryDecl {
                            source: ExposeSource::Child("netstack".to_string()),
                            source_path: "/data/dir".try_into().unwrap(),
                            target_path: "/data".try_into().unwrap(),
                            target: ExposeTarget::Framework,
                            rights: Some(fio2::Operations::Connect),
                        }),
                        ExposeDecl::Runner(ExposeRunnerDecl {
                            source: ExposeSource::Child("netstack".to_string()),
                            source_name: "elf".try_into().unwrap(),
                            target: ExposeTarget::Realm,
                            target_name: "elf".try_into().unwrap(),
                        }),
                        ExposeDecl::Service(ExposeServiceDecl {
                            sources: vec![
                                ServiceSource::<ExposeSource> {
                                    source: ExposeSource::Child("netstack".to_string()),
                                    source_path: "/svc/netstack1".try_into().unwrap(),
                                },
                                ServiceSource::<ExposeSource> {
                                    source: ExposeSource::Child("netstack".to_string()),
                                    source_path: "/svc/netstack2".try_into().unwrap(),
                                },
                            ],
                            target_path: "/svc/mynetstack".try_into().unwrap(),
                            target: ExposeTarget::Realm,
                        }),
                    ],
                    offers: vec![
                        OfferDecl::LegacyService(OfferLegacyServiceDecl {
                            source: OfferServiceSource::Realm,
                            source_path: "/svc/legacy_netstack".try_into().unwrap(),
                            target: OfferTarget::Child("echo".to_string()),
                            target_path: "/svc/legacy_mynetstack".try_into().unwrap(),
                        }),
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: OfferDirectorySource::Realm,
                            source_path: "/data/dir".try_into().unwrap(),
                            target: OfferTarget::Collection("modular".to_string()),
                            target_path: "/data".try_into().unwrap(),
                            rights: Some(fio2::Operations::Connect),
                        }),
                        OfferDecl::Storage(OfferStorageDecl::Cache(
                            OfferStorage {
                                source: OfferStorageSource::Storage("memfs".to_string()),
                                target: OfferTarget::Collection("modular".to_string()),
                            }
                        )),
                        OfferDecl::Runner(OfferRunnerDecl {
                            source: OfferRunnerSource::Realm,
                            source_name: "elf".try_into().unwrap(),
                            target: OfferTarget::Child("echo".to_string()),
                            target_name: "elf2".try_into().unwrap(),
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            sources: vec![
                                ServiceSource::<OfferServiceSource> {
                                    source: OfferServiceSource::Realm,
                                    source_path: "/svc/netstack1".try_into().unwrap(),
                                },
                                ServiceSource::<OfferServiceSource> {
                                    source: OfferServiceSource::Realm,
                                    source_path: "/svc/netstack2".try_into().unwrap(),
                                },
                            ],
                            target: OfferTarget::Child("echo".to_string()),
                            target_path: "/svc/mynetstack".try_into().unwrap(),
                        }),
                    ],
                    children: vec![
                        ChildDecl {
                            name: "netstack".to_string(),
                            url: "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cm".to_string(),
                            startup: fsys::StartupMode::Lazy,
                        },
                        ChildDecl {
                            name: "echo".to_string(),
                            url: "fuchsia-pkg://fuchsia.com/echo#meta/echo.cm".to_string(),
                            startup: fsys::StartupMode::Eager,
                        },
                    ],
                    collections: vec![
                        CollectionDecl {
                            name: "modular".to_string(),
                            durability: fsys::Durability::Persistent,
                        },
                        CollectionDecl {
                            name: "tests".to_string(),
                            durability: fsys::Durability::Transient,
                        },
                    ],
                    facets: Some(fdata::Dictionary{entries: vec![
                       fdata::Entry{
                           key: "author".to_string(),
                           value: Some(Box::new(fdata::Value::Str("Fuchsia".to_string()))),
                       },
                    ]}),
                    storage: vec![
                        StorageDecl {
                            name: "memfs".to_string(),
                            source_path: "/memfs".try_into().unwrap(),
                            source: StorageDirectorySource::Realm,
                        },
                    ],
                    runners: vec![
                        RunnerDecl {
                            name: "elf".to_string(),
                            source: RunnerSource::Self_,
                            source_path: "/elf".try_into().unwrap(),
                        }
                    ],
                }
            },
        },
    }

    test_capability_path! {
        capability_path_one_part => {
            input = "/foo",
            result = Ok(CapabilityPath{dirname: "/".to_string(), basename: "foo".to_string()}),
        },
        capability_path_two_parts => {
            input = "/foo/bar",
            result = Ok(CapabilityPath{dirname: "/foo".to_string(), basename: "bar".to_string()}),
        },
        capability_path_many_parts => {
            input = "/foo/bar/long/path",
            result = Ok(CapabilityPath{
                dirname: "/foo/bar/long".to_string(),
                basename: "path".to_string()
            }),
        },
        capability_path_invalid_empty_part => {
            input = "/foo/bar//long/path",
            result = Err(Error::InvalidCapabilityPath{raw: "/foo/bar//long/path".to_string()}),
        },
        capability_path_invalid_empty => {
            input = "",
            result = Err(Error::InvalidCapabilityPath{raw: "".to_string()}),
        },
        capability_path_invalid_root => {
            input = "/",
            result = Err(Error::InvalidCapabilityPath{raw: "/".to_string()}),
        },
        capability_path_invalid_relative => {
            input = "foo/bar",
            result = Err(Error::InvalidCapabilityPath{raw: "foo/bar".to_string()}),
        },
        capability_path_invalid_trailing => {
            input = "/foo/bar/",
            result = Err(Error::InvalidCapabilityPath{raw: "/foo/bar/".to_string()}),
        },
    }

    test_fidl_into_and_from! {
        fidl_into_and_from_expose_source => {
            input = vec![
                Some(fsys::Ref::Self_(fsys::SelfRef {})),
                Some(fsys::Ref::Child(fsys::ChildRef {
                    name: "foo".to_string(),
                    collection: None,
                })),
                Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
            ],
            input_type = Option<fsys::Ref>,
            result = vec![
                ExposeSource::Self_,
                ExposeSource::Child("foo".to_string()),
                ExposeSource::Framework,
            ],
            result_type = ExposeSource,
        },
        fidl_into_and_from_offer_service_source => {
            input = vec![
                Some(fsys::Ref::Realm(fsys::RealmRef {})),
                Some(fsys::Ref::Self_(fsys::SelfRef {})),
                Some(fsys::Ref::Child(fsys::ChildRef {
                    name: "foo".to_string(),
                    collection: None,
                })),
            ],
            input_type = Option<fsys::Ref>,
            result = vec![
                OfferServiceSource::Realm,
                OfferServiceSource::Self_,
                OfferServiceSource::Child("foo".to_string()),
            ],
            result_type = OfferServiceSource,
        },
        fidl_into_and_from_offer_directory_source => {
            input = vec![
                Some(fsys::Ref::Realm(fsys::RealmRef {})),
                Some(fsys::Ref::Self_(fsys::SelfRef {})),
                Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                Some(fsys::Ref::Child(fsys::ChildRef {
                    name: "foo".to_string(),
                    collection: None,
                })),
            ],
            input_type = Option<fsys::Ref>,
            result = vec![
                OfferDirectorySource::Realm,
                OfferDirectorySource::Self_,
                OfferDirectorySource::Framework,
                OfferDirectorySource::Child("foo".to_string()),
            ],
            result_type = OfferDirectorySource,
        },
        fidl_into_and_from_offer_storage_source => {
            input = vec![
                Some(fsys::Ref::Realm(fsys::RealmRef {})),
                Some(fsys::Ref::Storage(fsys::StorageRef {
                    name: "foo".to_string(),
                })),
            ],
            input_type = Option<fsys::Ref>,
            result = vec![
                OfferStorageSource::Realm,
                OfferStorageSource::Storage("foo".to_string()),
            ],
            result_type = OfferStorageSource,
        },
        fidl_into_and_from_storage_capability => {
            input = vec![
                fsys::StorageDecl {
                    name: Some("minfs".to_string()),
                    source_path: Some("/minfs".to_string()),
                    source: Some(fsys::Ref::Realm(fsys::RealmRef {})),
                },
                fsys::StorageDecl {
                    name: Some("minfs".to_string()),
                    source_path: Some("/minfs".to_string()),
                    source: Some(fsys::Ref::Child(fsys::ChildRef {
                        name: "foo".to_string(),
                        collection: None,
                    })),
                },
            ],
            input_type = fsys::StorageDecl,
            result = vec![
                StorageDecl {
                    name: "minfs".to_string(),
                    source_path: CapabilityPath::try_from("/minfs").unwrap(),
                    source: StorageDirectorySource::Realm,
                },
                StorageDecl {
                    name: "minfs".to_string(),
                    source_path: CapabilityPath::try_from("/minfs").unwrap(),
                    source: StorageDirectorySource::Child("foo".to_string()),
                },
            ],
            result_type = StorageDecl,
        },
    }

    test_fidl_into! {
        fidl_into_dictionary => {
            input = {
                let dict_inner = fdata::Dictionary{entries: vec![
                    fdata::Entry{
                        key: "string".to_string(),
                        value: Some(Box::new(fdata::Value::Str("bar".to_string()))),
                    },
                ]};
                let vector = fdata::Vector{values: vec![
                    Some(Box::new(fdata::Value::Dict(dict_inner))),
                    Some(Box::new(fdata::Value::Inum(-42)))
                ]};
                let dict_outer = fdata::Dictionary{entries: vec![
                    fdata::Entry{
                        key: "array".to_string(),
                        value: Some(Box::new(fdata::Value::Vec(vector))),
                    },
                ]};
                let dict = fdata::Dictionary {entries: vec![
                    fdata::Entry {
                        key: "bool".to_string(),
                        value: Some(Box::new(fdata::Value::Bit(true))),
                    },
                    fdata::Entry {
                        key: "dict".to_string(),
                        value: Some(Box::new(fdata::Value::Dict(dict_outer))),
                    },
                    fdata::Entry {
                        key: "float".to_string(),
                        value: Some(Box::new(fdata::Value::Fnum(3.14))),
                    },
                    fdata::Entry {
                        key: "int".to_string(),
                        value: Some(Box::new(fdata::Value::Inum(-42))),
                    },
                    fdata::Entry {
                        key: "null".to_string(),
                        value: None,
                    },
                    fdata::Entry {
                        key: "string".to_string(),
                        value: Some(Box::new(fdata::Value::Str("bar".to_string()))),
                    },
                ]};
                Some(dict)
            },
            result = {
                let mut dict_inner = HashMap::new();
                dict_inner.insert("string".to_string(), Value::Str("bar".to_string()));
                let mut dict_outer = HashMap::new();
                let vector = vec![Value::Dict(dict_inner), Value::Inum(-42)];
                dict_outer.insert("array".to_string(), Value::Vec(vector));

                let mut dict: HashMap<String, Value> = HashMap::new();
                dict.insert("bool".to_string(), Value::Bit(true));
                dict.insert("float".to_string(), Value::Fnum(3.14));
                dict.insert("int".to_string(), Value::Inum(-42));
                dict.insert("string".to_string(), Value::Str("bar".to_string()));
                dict.insert("dict".to_string(), Value::Dict(dict_outer));
                dict.insert("null".to_string(), Value::Null);
                Some(dict)
            },
        },
    }
}
