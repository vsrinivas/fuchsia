// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_fidl_validator, cm_types, fidl_fuchsia_data as fdata, fidl_fuchsia_io2 as fio2,
    fidl_fuchsia_sys2 as fsys,
    lazy_static::lazy_static,
    std::collections::HashMap,
    std::convert::{From, TryFrom, TryInto},
    std::fmt,
    std::path::PathBuf,
    std::str::FromStr,
    thiserror::Error,
};

pub mod data;

lazy_static! {
    static ref DATA_TYPENAME: CapabilityName = CapabilityName("Data".to_string());
    static ref CACHE_TYPENAME: CapabilityName = CapabilityName("Cache".to_string());
    static ref META_TYPENAME: CapabilityName = CapabilityName("Meta".to_string());
}

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
/// - `from_path` must be identical to `from_type`.
/// - `field: type` form a list of fields and their types for the generated struct.
macro_rules! fidl_into_struct {
    ($into_type:ty, $into_ident:ident, $from_type:ty, $from_path:path,
     { $( $field:ident: $type:ty, )+ } ) => {
        #[derive(Debug, Clone, PartialEq, Eq)]
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
        #[derive(Debug, Clone, PartialEq, Eq)]
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

#[derive(Debug, PartialEq, Default)]
pub struct ComponentDecl {
    pub program: Option<fdata::Dictionary>,
    pub uses: Vec<UseDecl>,
    pub exposes: Vec<ExposeDecl>,
    pub offers: Vec<OfferDecl>,
    pub capabilities: Vec<CapabilityDecl>,
    pub children: Vec<ChildDecl>,
    pub collections: Vec<CollectionDecl>,
    pub facets: Option<fsys::Object>,
    pub environments: Vec<EnvironmentDecl>,
}

impl FidlIntoNative<ComponentDecl> for fsys::ComponentDecl {
    fn fidl_into_native(self) -> ComponentDecl {
        // When transforming ExposeDecl::Service and OfferDecl::Service from
        // FIDL to native, we aggregate the declarations by target.
        let mut exposes = vec![];
        if let Some(e) = self.exposes {
            let mut services: HashMap<(ExposeTarget, CapabilityName), Vec<_>> = HashMap::new();
            for expose in e.into_iter() {
                match expose {
                    fsys::ExposeDecl::Service(s) => services
                        .entry((s.target.fidl_into_native(), s.target_name.fidl_into_native()))
                        .or_default()
                        .push(ServiceSource::<ExposeServiceSource> {
                            source: s.source.fidl_into_native(),
                            source_name: s.source_name.fidl_into_native(),
                        }),
                    fsys::ExposeDecl::Protocol(ls) => {
                        exposes.push(ExposeDecl::Protocol(ls.fidl_into_native()))
                    }
                    fsys::ExposeDecl::Directory(d) => {
                        exposes.push(ExposeDecl::Directory(d.fidl_into_native()))
                    }
                    fsys::ExposeDecl::Runner(r) => {
                        exposes.push(ExposeDecl::Runner(r.fidl_into_native()))
                    }
                    fsys::ExposeDecl::Resolver(r) => {
                        exposes.push(ExposeDecl::Resolver(r.fidl_into_native()))
                    }
                    fsys::ExposeDecl::__UnknownVariant { .. } => panic!("invalid variant"),
                }
            }
            for ((target, target_name), sources) in services.into_iter() {
                exposes.push(ExposeDecl::Service(ExposeServiceDecl {
                    sources,
                    target,
                    target_name,
                }))
            }
        }
        let mut offers = vec![];
        if let Some(o) = self.offers {
            let mut services: HashMap<(OfferTarget, CapabilityName), Vec<_>> = HashMap::new();
            for offer in o.into_iter() {
                match offer {
                    fsys::OfferDecl::Service(s) => services
                        .entry((s.target.fidl_into_native(), s.target_name.fidl_into_native()))
                        .or_default()
                        .push(ServiceSource::<OfferServiceSource> {
                            source: s.source.fidl_into_native(),
                            source_name: s.source_name.fidl_into_native(),
                        }),
                    fsys::OfferDecl::Protocol(ls) => {
                        offers.push(OfferDecl::Protocol(ls.fidl_into_native()))
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
                    fsys::OfferDecl::Resolver(r) => {
                        offers.push(OfferDecl::Resolver(r.fidl_into_native()))
                    }
                    fsys::OfferDecl::Event(e) => {
                        offers.push(OfferDecl::Event(e.fidl_into_native()))
                    }
                    fsys::OfferDecl::__UnknownVariant { .. } => panic!("invalid variant"),
                }
            }
            for ((target, target_name), sources) in services.into_iter() {
                offers.push(OfferDecl::Service(OfferServiceDecl { sources, target, target_name }))
            }
        }
        ComponentDecl {
            program: self.program.fidl_into_native(),
            uses: self.uses.fidl_into_native(),
            exposes,
            offers,
            capabilities: self.capabilities.fidl_into_native(),
            children: self.children.fidl_into_native(),
            collections: self.collections.fidl_into_native(),
            facets: self.facets.fidl_into_native(),
            environments: self.environments.fidl_into_native(),
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
                            source_name: es.source_name.native_into_fidl(),
                            target: s.target.clone().native_into_fidl(),
                            target_name: s.target_name.clone().native_into_fidl(),
                        }))
                    }
                }
                ExposeDecl::Protocol(ls) => {
                    exposes.push(fsys::ExposeDecl::Protocol(ls.native_into_fidl()))
                }
                ExposeDecl::Directory(d) => {
                    exposes.push(fsys::ExposeDecl::Directory(d.native_into_fidl()))
                }
                ExposeDecl::Runner(r) => {
                    exposes.push(fsys::ExposeDecl::Runner(r.native_into_fidl()))
                }
                ExposeDecl::Resolver(r) => {
                    exposes.push(fsys::ExposeDecl::Resolver(r.native_into_fidl()))
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
                            source_name: os.source_name.native_into_fidl(),
                            target: s.target.clone().native_into_fidl(),
                            target_name: s.target_name.clone().native_into_fidl(),
                        }))
                    }
                }
                OfferDecl::Protocol(ls) => {
                    offers.push(fsys::OfferDecl::Protocol(ls.native_into_fidl()))
                }
                OfferDecl::Directory(d) => {
                    offers.push(fsys::OfferDecl::Directory(d.native_into_fidl()))
                }
                OfferDecl::Storage(s) => {
                    offers.push(fsys::OfferDecl::Storage(s.native_into_fidl()))
                }
                OfferDecl::Runner(s) => offers.push(fsys::OfferDecl::Runner(s.native_into_fidl())),
                OfferDecl::Resolver(r) => {
                    offers.push(fsys::OfferDecl::Resolver(r.native_into_fidl()))
                }
                OfferDecl::Event(e) => offers.push(fsys::OfferDecl::Event(e.native_into_fidl())),
            }
        }
        fsys::ComponentDecl {
            program: self.program.native_into_fidl(),
            uses: self.uses.native_into_fidl(),
            exposes: if exposes.is_empty() { None } else { Some(exposes) },
            offers: if offers.is_empty() { None } else { Some(offers) },
            capabilities: self.capabilities.native_into_fidl(),
            children: self.children.native_into_fidl(),
            collections: self.collections.native_into_fidl(),
            facets: self.facets.native_into_fidl(),
            environments: self.environments.native_into_fidl(),
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
            capabilities: self.capabilities.clone(),
            children: self.children.clone(),
            collections: self.collections.clone(),
            facets: data::clone_option_object(&self.facets),
            environments: self.environments.clone(),
        }
    }
}

impl ComponentDecl {
    /// Returns the `UseRunnerDecl` for this component, or `None` if this is a non-executable
    /// component.
    pub fn get_used_runner(&self) -> Option<&UseRunnerDecl> {
        self.uses.iter().find_map(|u| match u {
            UseDecl::Runner(runner) => Some(runner),
            _ => return None,
        })
    }

    /// Returns the `StorageDecl` corresponding to `storage_name`.
    pub fn find_storage_source<'a>(&'a self, storage_name: &str) -> Option<&'a StorageDecl> {
        self.capabilities.iter().find_map(|c| {
            match c {
                CapabilityDecl::Storage(s) if &s.name == storage_name => {
                    return Some(s);
                }
                _ => {}
            }
            None
        })
    }

    /// Returns the `ProtocolDecl` corresponding to `protocol_name`.
    pub fn find_protocol_source<'a>(
        &'a self,
        protocol_name: &CapabilityName,
    ) -> Option<&'a ProtocolDecl> {
        self.capabilities.iter().find_map(|c| match c {
            CapabilityDecl::Protocol(r) if &r.name == protocol_name => Some(r),
            _ => None,
        })
    }

    /// Returns the `DirectoryDecl` corresponding to `directory_name`.
    pub fn find_directory_source<'a>(
        &'a self,
        directory_name: &CapabilityName,
    ) -> Option<&'a DirectoryDecl> {
        self.capabilities.iter().find_map(|c| match c {
            CapabilityDecl::Directory(r) if &r.name == directory_name => Some(r),
            _ => None,
        })
    }

    /// Returns the `RunnerDecl` corresponding to `runner_name`.
    pub fn find_runner_source<'a>(
        &'a self,
        runner_name: &CapabilityName,
    ) -> Option<&'a RunnerDecl> {
        self.capabilities.iter().find_map(|c| match c {
            CapabilityDecl::Runner(r) if &r.name == runner_name => Some(r),
            _ => None,
        })
    }

    /// Returns the `CollectionDecl` corresponding to `collection_name`.
    pub fn find_collection<'a>(&'a self, collection_name: &str) -> Option<&'a CollectionDecl> {
        self.collections.iter().find(|c| c.name == collection_name)
    }

    /// Indicates whether the capability specified by `target_name` is exposed to the framework.
    pub fn is_protocol_exposed_to_framework(&self, in_target_name: &CapabilityName) -> bool {
        self.exposes.iter().any(|expose| match expose {
            ExposeDecl::Protocol(ExposeProtocolDecl {
                target,
                target_path: target_name_or_path,
                ..
            }) if target == &ExposeTarget::Framework => {
                match target_name_or_path {
                    CapabilityNameOrPath::Name(name) => name == in_target_name,
                    CapabilityNameOrPath::Path(path) => {
                        // TODO(fxbug.dev/56604): Remove this legacy compatibility path
                        let res: Result<CapabilityPath, _> =
                            format!("/svc/{}", in_target_name).parse();
                        if res.is_err() {
                            return false;
                        }
                        let in_target_path = res.unwrap();
                        path == &in_target_path
                    }
                }
            }
            _ => false,
        })
    }

    /// Indicates whether the capability specified by `source_name` is requested.
    pub fn uses_protocol(&self, source_name: &CapabilityName) -> bool {
        self.uses.iter().any(|use_decl| match use_decl {
            UseDecl::Protocol(ls) => match &ls.source_path {
                CapabilityNameOrPath::Path(p) => {
                    let source_path: CapabilityPath =
                        format!("/svc/{}", source_name).parse().expect("bad path");
                    p == &source_path
                }
                CapabilityNameOrPath::Name(n) => n == source_name,
            },
            _ => false,
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ExposeDecl {
    Service(ExposeServiceDecl),
    Protocol(ExposeProtocolDecl),
    Directory(ExposeDirectoryDecl),
    Runner(ExposeRunnerDecl),
    Resolver(ExposeResolverDecl),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExposeServiceDecl {
    pub sources: Vec<ServiceSource<ExposeServiceSource>>,
    pub target: ExposeTarget,
    pub target_name: CapabilityName,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OfferDecl {
    Service(OfferServiceDecl),
    Protocol(OfferProtocolDecl),
    Directory(OfferDirectoryDecl),
    Storage(OfferStorageDecl),
    Runner(OfferRunnerDecl),
    Resolver(OfferResolverDecl),
    Event(OfferEventDecl),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OfferServiceDecl {
    pub sources: Vec<ServiceSource<OfferServiceSource>>,
    pub target: OfferTarget,
    pub target_name: CapabilityName,
}

fidl_into_enum!(UseDecl, UseDecl, fsys::UseDecl, fsys::UseDecl,
{
    Service(UseServiceDecl),
    Protocol(UseProtocolDecl),
    Directory(UseDirectoryDecl),
    Storage(UseStorageDecl),
    Runner(UseRunnerDecl),
    Event(UseEventDecl),
    EventStream(UseEventStreamDecl),
});
fidl_into_struct!(UseServiceDecl, UseServiceDecl, fsys::UseServiceDecl, fsys::UseServiceDecl,
{
    source: UseSource,
    source_name: CapabilityName,
    target_path: CapabilityPath,
});
fidl_into_struct!(UseProtocolDecl, UseProtocolDecl, fsys::UseProtocolDecl, fsys::UseProtocolDecl,
{
    source: UseSource,
    source_path: CapabilityNameOrPath,
    target_path: CapabilityPath,
});
fidl_into_struct!(UseDirectoryDecl, UseDirectoryDecl, fsys::UseDirectoryDecl,
fsys::UseDirectoryDecl,
{
    source: UseSource,
    source_path: CapabilityNameOrPath,
    target_path: CapabilityPath,
    rights: fio2::Operations,
    subdir: Option<PathBuf>,
});
fidl_into_struct!(UseRunnerDecl, UseRunnerDecl, fsys::UseRunnerDecl,
fsys::UseRunnerDecl,
{
    source_name: CapabilityName,
});
fidl_into_struct!(UseEventDecl, UseEventDecl, fsys::UseEventDecl,
fsys::UseEventDecl,
{
    source: UseSource,
    source_name: CapabilityName,
    target_name: CapabilityName,
    filter: Option<HashMap<String, DictionaryValue>>,
});
fidl_into_struct!(UseEventStreamDecl, UseEventStreamDecl, fsys::UseEventStreamDecl,
fsys::UseEventStreamDecl,
{
    target_path: CapabilityPath,
    events: Vec<String>,
});

fidl_into_struct!(ExposeProtocolDecl, ExposeProtocolDecl, fsys::ExposeProtocolDecl,
fsys::ExposeProtocolDecl,
{
    source: ExposeSource,
    source_path: CapabilityNameOrPath,
    target: ExposeTarget,
    target_path: CapabilityNameOrPath,
});
fidl_into_struct!(ExposeDirectoryDecl, ExposeDirectoryDecl, fsys::ExposeDirectoryDecl,
fsys::ExposeDirectoryDecl,
{
    source: ExposeSource,
    source_path: CapabilityNameOrPath,
    target: ExposeTarget,
    target_path: CapabilityNameOrPath,
    rights: Option<fio2::Operations>,
    subdir: Option<PathBuf>,
});
fidl_into_struct!(ExposeResolverDecl, ExposeResolverDecl, fsys::ExposeResolverDecl,
fsys::ExposeResolverDecl,
{
    source: ExposeSource,
    source_name: CapabilityName,
    target: ExposeTarget,
    target_name: CapabilityName,
});
fidl_into_struct!(ExposeRunnerDecl, ExposeRunnerDecl, fsys::ExposeRunnerDecl,
fsys::ExposeRunnerDecl,
{
    source: ExposeSource,
    source_name: CapabilityName,
    target: ExposeTarget,
    target_name: CapabilityName,
});
fidl_into_struct!(OfferProtocolDecl, OfferProtocolDecl, fsys::OfferProtocolDecl,
fsys::OfferProtocolDecl,
{
    source: OfferServiceSource,
    source_path: CapabilityNameOrPath,
    target: OfferTarget,
    target_path: CapabilityNameOrPath,
    dependency_type: DependencyType,
});
fidl_into_struct!(OfferDirectoryDecl, OfferDirectoryDecl, fsys::OfferDirectoryDecl,
fsys::OfferDirectoryDecl,
{
    source: OfferDirectorySource,
    source_path: CapabilityNameOrPath,
    target: OfferTarget,
    target_path: CapabilityNameOrPath,
    rights: Option<fio2::Operations>,
    subdir: Option<PathBuf>,
    dependency_type: DependencyType,
});
fidl_into_struct!(OfferResolverDecl, OfferResolverDecl, fsys::OfferResolverDecl,
fsys::OfferResolverDecl,
{
    source: OfferResolverSource,
    source_name: CapabilityName,
    target: OfferTarget,
    target_name: CapabilityName,
});
fidl_into_struct!(OfferRunnerDecl, OfferRunnerDecl, fsys::OfferRunnerDecl,
fsys::OfferRunnerDecl,
{
    source: OfferRunnerSource,
    source_name: CapabilityName,
    target: OfferTarget,
    target_name: CapabilityName,
});
fidl_into_struct!(OfferEventDecl, OfferEventDecl, fsys::OfferEventDecl,
fsys::OfferEventDecl,
{
    source: OfferEventSource,
    source_name: CapabilityName,
    target: OfferTarget,
    target_name: CapabilityName,
    filter: Option<HashMap<String, DictionaryValue>>,
});
fidl_into_enum!(CapabilityDecl, CapabilityDecl, fsys::CapabilityDecl, fsys::CapabilityDecl,
{
    Service(ServiceDecl),
    Protocol(ProtocolDecl),
    Directory(DirectoryDecl),
    Storage(StorageDecl),
    Runner(RunnerDecl),
    Resolver(ResolverDecl),
});
fidl_into_struct!(ServiceDecl, ServiceDecl, fsys::ServiceDecl, fsys::ServiceDecl,
{
    name: CapabilityName,
    source_path: CapabilityPath,
});
fidl_into_struct!(ProtocolDecl, ProtocolDecl, fsys::ProtocolDecl, fsys::ProtocolDecl,
{
    name: CapabilityName,
    source_path: CapabilityPath,
});
fidl_into_struct!(DirectoryDecl, DirectoryDecl, fsys::DirectoryDecl, fsys::DirectoryDecl,
{
    name: CapabilityName,
    source_path: CapabilityPath,
    rights: fio2::Operations,
});
fidl_into_struct!(StorageDecl, StorageDecl, fsys::StorageDecl,
fsys::StorageDecl,
{
    name: String,
    source: StorageDirectorySource,
    source_path: CapabilityNameOrPath,
});
fidl_into_struct!(RunnerDecl, RunnerDecl, fsys::RunnerDecl, fsys::RunnerDecl,
{
    name: CapabilityName,
    source: RunnerSource,
    source_path: CapabilityPath,
});
fidl_into_struct!(ResolverDecl, ResolverDecl, fsys::ResolverDecl, fsys::ResolverDecl,
{
    name: CapabilityName,
    source_path: CapabilityPath,
});
fidl_into_struct!(ChildDecl, ChildDecl, fsys::ChildDecl, fsys::ChildDecl,
{
    name: String,
    url: String,
    startup: fsys::StartupMode,
    environment: Option<String>,
});

fidl_into_struct!(CollectionDecl, CollectionDecl, fsys::CollectionDecl, fsys::CollectionDecl,
{
    name: String,
    durability: fsys::Durability,
    environment: Option<String>,
});
fidl_into_struct!(EnvironmentDecl, EnvironmentDecl, fsys::EnvironmentDecl, fsys::EnvironmentDecl,
{
    name: String,
    extends: fsys::EnvironmentExtends,
    runners: Vec<RunnerRegistration>,
    resolvers: Vec<ResolverRegistration>,
    stop_timeout_ms: Option<u32>,
});
fidl_into_struct!(RunnerRegistration, RunnerRegistration, fsys::RunnerRegistration,
fsys::RunnerRegistration,
{
    source_name: CapabilityName,
    source: RegistrationSource,
    target_name: CapabilityName,
});
fidl_into_struct!(ResolverRegistration, ResolverRegistration, fsys::ResolverRegistration,
fsys::ResolverRegistration,
{
    resolver: CapabilityName,
    source: RegistrationSource,
    scheme: String,
});

fidl_into_vec!(UseDecl, fsys::UseDecl);
fidl_into_vec!(ChildDecl, fsys::ChildDecl);
fidl_into_vec!(CollectionDecl, fsys::CollectionDecl);
fidl_into_vec!(CapabilityDecl, fsys::CapabilityDecl);
fidl_into_vec!(EnvironmentDecl, fsys::EnvironmentDecl);
fidl_into_vec!(RunnerRegistration, fsys::RunnerRegistration);
fidl_into_vec!(ResolverRegistration, fsys::ResolverRegistration);
fidl_translations_opt_type!(Vec<String>);
fidl_translations_opt_type!(String);
fidl_translations_opt_type!(fsys::StartupMode);
fidl_translations_opt_type!(fsys::Durability);
fidl_translations_opt_type!(fsys::Object);
fidl_translations_opt_type!(fdata::Dictionary);
fidl_translations_opt_type!(fio2::Operations);
fidl_translations_opt_type!(fsys::EnvironmentExtends);
fidl_translations_identical!(Option<fio2::Operations>);
fidl_translations_identical!(Option<fsys::Object>);
fidl_translations_identical!(Option<fdata::Dictionary>);
fidl_translations_identical!(Option<String>);

/// A path to a capability.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct CapabilityPath {
    /// The directory containing the last path element, e.g. `/svc/foo` in `/svc/foo/bar`.
    pub dirname: String,
    /// The last path element: e.g. `bar` in `/svc/foo/bar`.
    pub basename: String,
}

impl CapabilityPath {
    pub fn to_path_buf(&self) -> PathBuf {
        PathBuf::from(self.to_string())
    }

    /// Splits the path according to "/", ignoring empty path components
    pub fn split(&self) -> Vec<String> {
        self.to_string().split("/").map(|s| s.to_string()).filter(|s| !s.is_empty()).collect()
    }
}

impl FromStr for CapabilityPath {
    type Err = Error;

    fn from_str(path: &str) -> Result<CapabilityPath, Error> {
        cm_types::Path::validate(path)
            .map_err(|_| Error::InvalidCapabilityPath { raw: path.to_string() })?;
        let idx = path.rfind('/').expect("path validation is wrong");
        Ok(CapabilityPath {
            dirname: if idx == 0 { "/".to_string() } else { path[0..idx].to_string() },
            basename: path[idx + 1..].to_string(),
        })
    }
}

impl TryFrom<&str> for CapabilityPath {
    type Error = Error;

    fn try_from(path: &str) -> Result<CapabilityPath, Error> {
        Self::from_str(path)
    }
}

impl fmt::Display for CapabilityPath {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
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
            UseDecl::Protocol(d) => &d.target_path,
            UseDecl::Directory(d) => &d.target_path,
            UseDecl::Storage(UseStorageDecl::Data(p)) => &p,
            UseDecl::Storage(UseStorageDecl::Cache(p)) => &p,
            UseDecl::EventStream(e) => &e.target_path,
            UseDecl::Storage(UseStorageDecl::Meta) | UseDecl::Runner(_) | UseDecl::Event(_) => {
                // Meta storage and runners don't show up in the namespace; no capability path.
                return None;
            }
        };
        Some(path)
    }

    pub fn name(&self) -> Option<&CapabilityName> {
        match self {
            UseDecl::Event(event_decl) => Some(&event_decl.source_name),
            UseDecl::Runner(runner_decl) => Some(&runner_decl.source_name),
            UseDecl::Service(_)
            | UseDecl::Protocol(_)
            | UseDecl::Directory(_)
            | UseDecl::Storage(_)
            | UseDecl::EventStream(_) => None,
        }
    }
}

/// A named capability.
///
/// Unlike a `CapabilityPath`, a `CapabilityName` doesn't encode any form
/// of hierarchy.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct CapabilityName(pub String);

impl CapabilityName {
    pub fn str(&self) -> &str {
        &self.0
    }
}

impl From<&str> for CapabilityName {
    fn from(name: &str) -> CapabilityName {
        CapabilityName(name.to_string())
    }
}

impl From<String> for CapabilityName {
    fn from(name: String) -> CapabilityName {
        CapabilityName(name)
    }
}

impl fmt::Display for CapabilityName {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

/// A capability identifier that can be either a name or path.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum CapabilityNameOrPath {
    Name(CapabilityName),
    Path(CapabilityPath),
}

impl FromStr for CapabilityNameOrPath {
    type Err = Error;

    fn from_str(s: &str) -> Result<CapabilityNameOrPath, Error> {
        if s.starts_with('/') {
            Ok(Self::Path(s.parse()?))
        } else {
            Ok(Self::Name(s.into()))
        }
    }
}

impl TryFrom<&str> for CapabilityNameOrPath {
    type Error = Error;

    fn try_from(s: &str) -> Result<CapabilityNameOrPath, Error> {
        Self::from_str(s)
    }
}

impl fmt::Display for CapabilityNameOrPath {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Name(n) => n.fmt(f),
            Self::Path(p) => p.fmt(f),
        }
    }
}

// TODO: Runners and third parties can use this to parse `facets`.
impl FidlIntoNative<Option<HashMap<String, Value>>> for Option<fsys::Object> {
    fn fidl_into_native(self) -> Option<HashMap<String, Value>> {
        self.map(|o| from_fidl_obj(o))
    }
}

impl FidlIntoNative<Option<HashMap<String, DictionaryValue>>> for Option<fdata::Dictionary> {
    fn fidl_into_native(self) -> Option<HashMap<String, DictionaryValue>> {
        self.map(|d| from_fidl_dict(d))
    }
}

impl NativeIntoFidl<Option<fdata::Dictionary>> for Option<HashMap<String, DictionaryValue>> {
    fn native_into_fidl(self) -> Option<fdata::Dictionary> {
        self.map(|d| to_fidl_dict(d))
    }
}

fidl_translations_identical!(Option<u32>);

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

impl FidlIntoNative<Option<PathBuf>> for Option<String> {
    fn fidl_into_native(self) -> Option<PathBuf> {
        self.map(|p| PathBuf::from(p))
    }
}

impl NativeIntoFidl<Option<String>> for Option<PathBuf> {
    fn native_into_fidl(self) -> Option<String> {
        self.map(|p| p.to_str().expect("invalid utf8").to_string())
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

impl FidlIntoNative<CapabilityNameOrPath> for Option<String> {
    fn fidl_into_native(self) -> CapabilityNameOrPath {
        let s: &str = &self.unwrap();
        s.try_into().expect("invalid name or path")
    }
}

impl NativeIntoFidl<Option<String>> for CapabilityNameOrPath {
    fn native_into_fidl(self) -> Option<String> {
        Some(self.to_string())
    }
}

impl FidlIntoNative<bool> for Option<bool> {
    fn fidl_into_native(self) -> bool {
        self.unwrap()
    }
}

impl NativeIntoFidl<Option<bool>> for bool {
    fn native_into_fidl(self) -> Option<bool> {
        Some(self)
    }
}

#[derive(Debug, PartialEq)]
pub enum Value {
    Bit(bool),
    Inum(i64),
    Fnum(f64),
    Str(String),
    Vec(Vec<Value>),
    Obj(HashMap<String, Value>),
    Null,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DictionaryValue {
    Str(String),
    StrVec(Vec<String>),
    Null,
}

impl FidlIntoNative<Value> for Option<Box<fsys::Value>> {
    fn fidl_into_native(self) -> Value {
        match self {
            Some(v) => match *v {
                fsys::Value::Bit(b) => Value::Bit(b),
                fsys::Value::Inum(i) => Value::Inum(i),
                fsys::Value::Fnum(f) => Value::Fnum(f),
                fsys::Value::Str(s) => Value::Str(s),
                fsys::Value::Vec(v) => Value::Vec(from_fidl_vec(v)),
                fsys::Value::Obj(d) => Value::Obj(from_fidl_obj(d)),
            },
            None => Value::Null,
        }
    }
}

impl FidlIntoNative<DictionaryValue> for Option<Box<fdata::DictionaryValue>> {
    fn fidl_into_native(self) -> DictionaryValue {
        match self {
            Some(v) => match *v {
                fdata::DictionaryValue::Str(s) => DictionaryValue::Str(s),
                fdata::DictionaryValue::StrVec(ss) => DictionaryValue::StrVec(ss),
            },
            None => DictionaryValue::Null,
        }
    }
}

impl NativeIntoFidl<Option<Box<fdata::DictionaryValue>>> for DictionaryValue {
    fn native_into_fidl(self) -> Option<Box<fdata::DictionaryValue>> {
        match self {
            DictionaryValue::Str(s) => Some(Box::new(fdata::DictionaryValue::Str(s))),
            DictionaryValue::StrVec(ss) => Some(Box::new(fdata::DictionaryValue::StrVec(ss))),
            DictionaryValue::Null => None,
        }
    }
}

fn from_fidl_vec(vec: fsys::Vector) -> Vec<Value> {
    vec.values.into_iter().map(|v| v.fidl_into_native()).collect()
}

fn from_fidl_obj(obj: fsys::Object) -> HashMap<String, Value> {
    obj.entries.into_iter().map(|e| (e.key, e.value.fidl_into_native())).collect()
}

fn from_fidl_dict(dict: fdata::Dictionary) -> HashMap<String, DictionaryValue> {
    match dict.entries {
        Some(entries) => entries.into_iter().map(|e| (e.key, e.value.fidl_into_native())).collect(),
        _ => HashMap::new(),
    }
}

fn to_fidl_dict(dict: HashMap<String, DictionaryValue>) -> fdata::Dictionary {
    fdata::Dictionary {
        entries: Some(
            dict.into_iter()
                .map(|(key, value)| fdata::DictionaryEntry { key, value: value.native_into_fidl() })
                .collect(),
        ),
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
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

    pub fn type_name(&self) -> &CapabilityName {
        match self {
            Self::Data(_) => &DATA_TYPENAME,
            Self::Cache(_) => &CACHE_TYPENAME,
            Self::Meta => &META_TYPENAME,
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OfferStorageDecl {
    Data(OfferStorage),
    Cache(OfferStorage),
    Meta(OfferStorage),
}

#[derive(Debug, Clone, PartialEq, Eq)]
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

    pub fn type_name(&self) -> &CapabilityName {
        match self {
            Self::Data(_) => &DATA_TYPENAME,
            Self::Cache(_) => &CACHE_TYPENAME,
            Self::Meta(_) => &META_TYPENAME,
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UseSource {
    Parent,
    Framework,
}

impl FidlIntoNative<UseSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> UseSource {
        match self.unwrap() {
            fsys::Ref::Parent(_) => UseSource::Parent,
            fsys::Ref::Framework(_) => UseSource::Framework,
            _ => panic!("invalid UseSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for UseSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            UseSource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            UseSource::Framework => fsys::Ref::Framework(fsys::FrameworkRef {}),
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
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
    Parent,
    Framework,
}

impl FidlIntoNative<ExposeTarget> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> ExposeTarget {
        match self {
            Some(dest) => match dest {
                fsys::Ref::Parent(_) => ExposeTarget::Parent,
                fsys::Ref::Framework(_) => ExposeTarget::Framework,
                _ => panic!("invalid ExposeTarget variant"),
            },
            None => ExposeTarget::Parent,
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for ExposeTarget {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            ExposeTarget::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            ExposeTarget::Framework => fsys::Ref::Framework(fsys::FrameworkRef {}),
        })
    }
}

/// A source for a service.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ServiceSource<T> {
    /// The provider of the service, relative to a component.
    pub source: T,
    /// The name of the service.
    pub source_name: CapabilityName,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DependencyType {
    Strong,
    WeakForMigration,
}

impl FidlIntoNative<DependencyType> for Option<fsys::DependencyType> {
    fn fidl_into_native(self) -> DependencyType {
        match self.unwrap() {
            fsys::DependencyType::Strong => DependencyType::Strong,
            fsys::DependencyType::WeakForMigration => DependencyType::WeakForMigration,
        }
    }
}

impl NativeIntoFidl<Option<fsys::DependencyType>> for DependencyType {
    fn native_into_fidl(self) -> Option<fsys::DependencyType> {
        Some(match self {
            DependencyType::Strong => fsys::DependencyType::Strong,
            DependencyType::WeakForMigration => fsys::DependencyType::WeakForMigration,
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OfferServiceSource {
    Parent,
    Self_,
    Child(String),
}

impl FidlIntoNative<OfferServiceSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferServiceSource {
        match self.unwrap() {
            fsys::Ref::Parent(_) => OfferServiceSource::Parent,
            fsys::Ref::Self_(_) => OfferServiceSource::Self_,
            fsys::Ref::Child(c) => OfferServiceSource::Child(c.name),
            _ => panic!("invalid OfferServiceSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for OfferServiceSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            OfferServiceSource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            OfferServiceSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            OfferServiceSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

/// The valid sources of a service protocol's expose declaration.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ExposeServiceSource {
    /// The service is exposed from the component manager itself.
    Framework,
    /// The service is exposed by the component itself.
    Self_,
    /// The service is exposed by a named child component.
    Child(String),
}

impl FidlIntoNative<ExposeServiceSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> ExposeServiceSource {
        match self.unwrap() {
            fsys::Ref::Framework(_) => ExposeServiceSource::Framework,
            fsys::Ref::Self_(_) => ExposeServiceSource::Self_,
            fsys::Ref::Child(c) => ExposeServiceSource::Child(c.name),
            _ => panic!("invalid ExposeServiceSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for ExposeServiceSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            ExposeServiceSource::Framework => fsys::Ref::Framework(fsys::FrameworkRef {}),
            ExposeServiceSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            ExposeServiceSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum StorageDirectorySource {
    Parent,
    Self_,
    Child(String),
}

impl FidlIntoNative<StorageDirectorySource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> StorageDirectorySource {
        match self.unwrap() {
            fsys::Ref::Parent(_) => StorageDirectorySource::Parent,
            fsys::Ref::Self_(_) => StorageDirectorySource::Self_,
            fsys::Ref::Child(c) => StorageDirectorySource::Child(c.name),
            _ => panic!("invalid OfferDirectorySource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for StorageDirectorySource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            StorageDirectorySource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            StorageDirectorySource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            StorageDirectorySource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RunnerSource {
    Parent,
    Self_,
    Child(String),
}

impl FidlIntoNative<RunnerSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> RunnerSource {
        match self.unwrap() {
            fsys::Ref::Parent(_) => RunnerSource::Parent,
            fsys::Ref::Self_(_) => RunnerSource::Self_,
            fsys::Ref::Child(c) => RunnerSource::Child(c.name),
            _ => panic!("invalid RunnerSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for RunnerSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            RunnerSource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            RunnerSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            RunnerSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ResolverSource {
    Parent,
    Self_,
    Child(String),
}

impl FidlIntoNative<ResolverSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> ResolverSource {
        match self.unwrap() {
            fsys::Ref::Parent(_) => ResolverSource::Parent,
            fsys::Ref::Self_(_) => ResolverSource::Self_,
            fsys::Ref::Child(c) => ResolverSource::Child(c.name),
            _ => panic!("invalid ResolverSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for ResolverSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            ResolverSource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            ResolverSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            ResolverSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RegistrationSource {
    Parent,
    Self_,
    Child(String),
}

impl FidlIntoNative<RegistrationSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> RegistrationSource {
        match self.unwrap() {
            fsys::Ref::Parent(_) => RegistrationSource::Parent,
            fsys::Ref::Self_(_) => RegistrationSource::Self_,
            fsys::Ref::Child(c) => RegistrationSource::Child(c.name),
            _ => panic!("invalid RegistrationSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for RegistrationSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            RegistrationSource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            RegistrationSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            RegistrationSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OfferDirectorySource {
    Parent,
    Self_,
    Framework,
    Child(String),
}

impl FidlIntoNative<OfferDirectorySource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferDirectorySource {
        match self.unwrap() {
            fsys::Ref::Parent(_) => OfferDirectorySource::Parent,
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
            OfferDirectorySource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            OfferDirectorySource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            OfferDirectorySource::Framework => fsys::Ref::Framework(fsys::FrameworkRef {}),
            OfferDirectorySource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OfferStorageSource {
    Parent,
    Storage(String),
}

impl FidlIntoNative<OfferStorageSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferStorageSource {
        match self.unwrap() {
            fsys::Ref::Parent(_) => OfferStorageSource::Parent,
            fsys::Ref::Storage(c) => OfferStorageSource::Storage(c.name),
            _ => panic!("invalid OfferStorageSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for OfferStorageSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            OfferStorageSource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            OfferStorageSource::Storage(name) => fsys::Ref::Storage(fsys::StorageRef { name }),
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OfferRunnerSource {
    Parent,
    Self_,
    Child(String),
}

impl FidlIntoNative<OfferRunnerSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferRunnerSource {
        match self.unwrap() {
            fsys::Ref::Parent(_) => OfferRunnerSource::Parent,
            fsys::Ref::Self_(_) => OfferRunnerSource::Self_,
            fsys::Ref::Child(c) => OfferRunnerSource::Child(c.name),
            _ => panic!("invalid OfferRunnerSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for OfferRunnerSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            OfferRunnerSource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            OfferRunnerSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            OfferRunnerSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OfferResolverSource {
    Parent,
    Self_,
    Child(String),
}

impl FidlIntoNative<OfferResolverSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferResolverSource {
        match self.unwrap() {
            fsys::Ref::Parent(_) => OfferResolverSource::Parent,
            fsys::Ref::Self_(_) => OfferResolverSource::Self_,
            fsys::Ref::Child(c) => OfferResolverSource::Child(c.name),
            _ => panic!("invalid OfferResolverSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for OfferResolverSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            OfferResolverSource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
            OfferResolverSource::Self_ => fsys::Ref::Self_(fsys::SelfRef {}),
            OfferResolverSource::Child(child_name) => {
                fsys::Ref::Child(fsys::ChildRef { name: child_name, collection: None })
            }
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OfferEventSource {
    Framework,
    Parent,
}

impl FidlIntoNative<OfferEventSource> for Option<fsys::Ref> {
    fn fidl_into_native(self) -> OfferEventSource {
        match self.unwrap() {
            fsys::Ref::Framework(_) => OfferEventSource::Framework,
            fsys::Ref::Parent(_) => OfferEventSource::Parent,
            _ => panic!("invalid OfferEventSource variant"),
        }
    }
}

impl NativeIntoFidl<Option<fsys::Ref>> for OfferEventSource {
    fn native_into_fidl(self) -> Option<fsys::Ref> {
        Some(match self {
            OfferEventSource::Framework => fsys::Ref::Framework(fsys::FrameworkRef {}),
            OfferEventSource::Parent => fsys::Ref::Parent(fsys::ParentRef {}),
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
#[derive(Debug, Error)]
pub enum Error {
    #[error("Fidl validation failed: {}", err)]
    Validate {
        #[source]
        err: cm_fidl_validator::ErrorList,
    },
    #[error("Invalid capability path: {}", raw)]
    InvalidCapabilityPath { raw: String },
}

#[cfg(test)]
mod tests {
    use {super::*, maplit::hashmap};

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
                capabilities: None,
                children: None,
                collections: None,
                facets: None,
                environments: None,
            },
            result = ComponentDecl {
                program: None,
                uses: vec![],
                exposes: vec![],
                offers: vec![],
                capabilities: vec![],
                children: vec![],
                collections: vec![],
                facets: None,
                environments: vec![],
            },
        },
        try_from_all => {
            input = fsys::ComponentDecl {
               program: Some(fdata::Dictionary{entries: Some(vec![
                   fdata::DictionaryEntry {
                       key: "args".to_string(),
                       value: Some(Box::new(fdata::DictionaryValue::StrVec(vec!["foo".to_string(), "bar".to_string()]))),
                   },
                   fdata::DictionaryEntry {
                       key: "binary".to_string(),
                       value: Some(Box::new(fdata::DictionaryValue::Str("bin/app".to_string()))),
                   },
               ])}),
               uses: Some(vec![
                   fsys::UseDecl::Service(fsys::UseServiceDecl {
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       source_name: Some("netstack".to_string()),
                       target_path: Some("/svc/mynetstack".to_string()),
                   }),
                   fsys::UseDecl::Protocol(fsys::UseProtocolDecl {
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       source_path: Some("/svc/legacy_netstack".to_string()),
                       target_path: Some("/svc/legacy_mynetstack".to_string()),
                   }),
                   fsys::UseDecl::Directory(fsys::UseDirectoryDecl {
                       source: Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                       source_path: Some("/data/dir".to_string()),
                       target_path: Some("/data".to_string()),
                       rights: Some(fio2::Operations::Connect),
                       subdir: Some("foo/bar".to_string()),
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
                   fsys::UseDecl::Event(fsys::UseEventDecl {
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       source_name: Some("capability_ready".to_string()),
                       target_name: Some("diagnostics_ready".to_string()),
                       filter: Some(fdata::Dictionary{
                           entries: Some(vec![
                              fdata::DictionaryEntry {
                                  key: "path".to_string(),
                                  value: Some(Box::new(fdata::DictionaryValue::Str("/diagnostics".to_string()))),
                              },
                           ])
                       }),
                   }),
               ]),
               exposes: Some(vec![
                   fsys::ExposeDecl::Protocol(fsys::ExposeProtocolDecl {
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_path: Some("/svc/legacy_netstack".to_string()),
                       target_path: Some("/svc/legacy_mynetstack".to_string()),
                       target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                   }),
                   fsys::ExposeDecl::Directory(fsys::ExposeDirectoryDecl {
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_path: Some("/data/dir".to_string()),
                       target_path: Some("/data".to_string()),
                       target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       rights: Some(fio2::Operations::Connect),
                       subdir: Some("foo/bar".to_string()),
                   }),
                   fsys::ExposeDecl::Runner(fsys::ExposeRunnerDecl {
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_name: Some("elf".to_string()),
                       target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       target_name: Some("elf".to_string()),
                   }),
                   fsys::ExposeDecl::Resolver(fsys::ExposeResolverDecl{
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_name: Some("pkg".to_string()),
                       target: Some(fsys::Ref::Parent(fsys::ParentRef{})),
                       target_name: Some("pkg".to_string()),
                   }),
                   fsys::ExposeDecl::Service(fsys::ExposeServiceDecl {
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_name: Some("netstack1".to_string()),
                       target_name: Some("mynetstack".to_string()),
                       target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                   }),
                   fsys::ExposeDecl::Service(fsys::ExposeServiceDecl {
                       source: Some(fsys::Ref::Child(fsys::ChildRef {
                           name: "netstack".to_string(),
                           collection: None,
                       })),
                       source_name: Some("netstack2".to_string()),
                       target_name: Some("mynetstack".to_string()),
                       target: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                   }),
               ]),
               offers: Some(vec![
                   fsys::OfferDecl::Protocol(fsys::OfferProtocolDecl {
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       source_path: Some("/svc/legacy_netstack".to_string()),
                       target: Some(fsys::Ref::Child(
                          fsys::ChildRef {
                              name: "echo".to_string(),
                              collection: None,
                          }
                       )),
                       target_path: Some("/svc/legacy_mynetstack".to_string()),
                       dependency_type: Some(fsys::DependencyType::WeakForMigration),
                   }),
                   fsys::OfferDecl::Directory(fsys::OfferDirectoryDecl {
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       source_path: Some("/data/dir".to_string()),
                       target: Some(fsys::Ref::Collection(
                           fsys::CollectionRef { name: "modular".to_string() }
                       )),
                       target_path: Some("/data".to_string()),
                       rights: Some(fio2::Operations::Connect),
                       subdir: None,
                       dependency_type: Some(fsys::DependencyType::Strong),
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
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       source_name: Some("elf".to_string()),
                       target: Some(fsys::Ref::Child(
                          fsys::ChildRef {
                              name: "echo".to_string(),
                              collection: None,
                          }
                       )),
                       target_name: Some("elf2".to_string()),
                   }),
                   fsys::OfferDecl::Resolver(fsys::OfferResolverDecl{
                       source: Some(fsys::Ref::Parent(fsys::ParentRef{})),
                       source_name: Some("pkg".to_string()),
                       target: Some(fsys::Ref::Child(
                          fsys::ChildRef {
                             name: "echo".to_string(),
                             collection: None,
                          }
                       )),
                       target_name: Some("pkg".to_string()),
                   }),
                   fsys::OfferDecl::Event(fsys::OfferEventDecl {
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       source_name: Some("started".to_string()),
                       target: Some(fsys::Ref::Child(
                          fsys::ChildRef {
                              name: "echo".to_string(),
                              collection: None,
                          }
                       )),
                       target_name: Some("mystarted".to_string()),
                       filter: Some(fdata::Dictionary {
                           entries: Some(vec![
                              fdata::DictionaryEntry {
                                  key: "path".to_string(),
                                  value: Some(Box::new(fdata::DictionaryValue::Str("/a".to_string()))),
                              },
                           ]),
                       }),
                   }),
                   fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       source_name: Some("netstack1".to_string()),
                       target: Some(fsys::Ref::Child(
                          fsys::ChildRef {
                              name: "echo".to_string(),
                              collection: None,
                          }
                       )),
                       target_name: Some("mynetstack".to_string()),
                   }),
                   fsys::OfferDecl::Service(fsys::OfferServiceDecl {
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                       source_name: Some("netstack2".to_string()),
                       target: Some(fsys::Ref::Child(
                          fsys::ChildRef {
                              name: "echo".to_string(),
                              collection: None,
                          }
                       )),
                       target_name: Some("mynetstack".to_string()),
                   }),
               ]),
               capabilities: Some(vec![
                   fsys::CapabilityDecl::Service(fsys::ServiceDecl {
                       name: Some("netstack".to_string()),
                       source_path: Some("/netstack".to_string()),
                   }),
                   fsys::CapabilityDecl::Protocol(fsys::ProtocolDecl {
                       name: Some("netstack2".to_string()),
                       source_path: Some("/netstack2".to_string()),
                   }),
                   fsys::CapabilityDecl::Directory(fsys::DirectoryDecl {
                       name: Some("data".to_string()),
                       source_path: Some("/data".to_string()),
                       rights: Some(fio2::Operations::Connect),
                   }),
                   fsys::CapabilityDecl::Storage(fsys::StorageDecl {
                       name: Some("memfs".to_string()),
                       source_path: Some("data".to_string()),
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                   }),
                   fsys::CapabilityDecl::Storage(fsys::StorageDecl {
                       name: Some("memfs2".to_string()),
                       source_path: Some("/data".to_string()),
                       source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                   }),
                   fsys::CapabilityDecl::Runner(fsys::RunnerDecl {
                       name: Some("elf".to_string()),
                       source_path: Some("/elf".to_string()),
                       source: Some(fsys::Ref::Self_(fsys::SelfRef {})),
                   }),
                   fsys::CapabilityDecl::Resolver(fsys::ResolverDecl {
                       name: Some("pkg".to_string()),
                       source_path: Some("/pkg_resolver".to_string()),
                   }),
               ]),
               children: Some(vec![
                    fsys::ChildDecl {
                        name: Some("netstack".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cm"
                                  .to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                    },
                    fsys::ChildDecl {
                        name: Some("gtest".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/gtest#meta/gtest.cm".to_string()),
                        startup: Some(fsys::StartupMode::Lazy),
                        environment: None,
                    },
                    fsys::ChildDecl {
                        name: Some("echo".to_string()),
                        url: Some("fuchsia-pkg://fuchsia.com/echo#meta/echo.cm"
                                  .to_string()),
                        startup: Some(fsys::StartupMode::Eager),
                        environment: Some("test_env".to_string()),
                    },
               ]),
               collections: Some(vec![
                    fsys::CollectionDecl {
                        name: Some("modular".to_string()),
                        durability: Some(fsys::Durability::Persistent),
                        environment: None,
                    },
                    fsys::CollectionDecl {
                        name: Some("tests".to_string()),
                        durability: Some(fsys::Durability::Transient),
                        environment: Some("test_env".to_string()),
                    },
               ]),
               facets: Some(fsys::Object{entries: vec![
                   fsys::Entry{
                       key: "author".to_string(),
                       value: Some(Box::new(fsys::Value::Str("Fuchsia".to_string()))),
                   },
               ]}),
               environments: Some(vec![
                   fsys::EnvironmentDecl {
                       name: Some("test_env".to_string()),
                       extends: Some(fsys::EnvironmentExtends::Realm),
                       runners: Some(vec![
                           fsys::RunnerRegistration {
                               source_name: Some("runner".to_string()),
                               source: Some(fsys::Ref::Child(fsys::ChildRef {
                                   name: "gtest".to_string(),
                                   collection: None,
                               })),
                               target_name: Some("gtest-runner".to_string()),
                           }
                       ]),
                       resolvers: Some(vec![
                           fsys::ResolverRegistration {
                               resolver: Some("pkg_resolver".to_string()),
                               source: Some(fsys::Ref::Parent(fsys::ParentRef{})),
                               scheme: Some("fuchsia-pkg".to_string()),
                           }
                       ]),
                       stop_timeout_ms: Some(4567),
                   }
               ]),
            },
            result = {
                ComponentDecl {
                    program: Some(fdata::Dictionary{entries: Some(vec![
                        fdata::DictionaryEntry {
                            key: "args".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::StrVec(vec!["foo".to_string(), "bar".to_string()]))),
                        },
                        fdata::DictionaryEntry{
                            key: "binary".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("bin/app".to_string()))),
                        },
                    ])}),
                    uses: vec![
                        UseDecl::Service(UseServiceDecl {
                            source: UseSource::Parent,
                            source_name: "netstack".try_into().unwrap(),
                            target_path: "/svc/mynetstack".try_into().unwrap(),
                        }),
                        UseDecl::Protocol(UseProtocolDecl {
                            source: UseSource::Parent,
                            source_path: "/svc/legacy_netstack".try_into().unwrap(),
                            target_path: "/svc/legacy_mynetstack".try_into().unwrap(),
                        }),
                        UseDecl::Directory(UseDirectoryDecl {
                            source: UseSource::Framework,
                            source_path: "/data/dir".try_into().unwrap(),
                            target_path: "/data".try_into().unwrap(),
                            rights: fio2::Operations::Connect,
                            subdir: Some("foo/bar".into()),
                        }),
                        UseDecl::Storage(UseStorageDecl::Cache("/cache".try_into().unwrap())),
                        UseDecl::Storage(UseStorageDecl::Meta),
                        UseDecl::Runner(UseRunnerDecl {
                            source_name: "myrunner".into(),
                        }),
                        UseDecl::Event(UseEventDecl {
                            source: UseSource::Parent,
                            source_name: "capability_ready".into(),
                            target_name: "diagnostics_ready".into(),
                            filter: Some(hashmap!{"path".to_string() =>  DictionaryValue::Str("/diagnostics".to_string())}),
                        })
                    ],
                    exposes: vec![
                        ExposeDecl::Protocol(ExposeProtocolDecl {
                            source: ExposeSource::Child("netstack".to_string()),
                            source_path: "/svc/legacy_netstack".try_into().unwrap(),
                            target_path: "/svc/legacy_mynetstack".try_into().unwrap(),
                            target: ExposeTarget::Parent,
                        }),
                        ExposeDecl::Directory(ExposeDirectoryDecl {
                            source: ExposeSource::Child("netstack".to_string()),
                            source_path: "/data/dir".try_into().unwrap(),
                            target_path: "/data".try_into().unwrap(),
                            target: ExposeTarget::Parent,
                            rights: Some(fio2::Operations::Connect),
                            subdir: Some("foo/bar".into()),
                        }),
                        ExposeDecl::Runner(ExposeRunnerDecl {
                            source: ExposeSource::Child("netstack".to_string()),
                            source_name: "elf".try_into().unwrap(),
                            target: ExposeTarget::Parent,
                            target_name: "elf".try_into().unwrap(),
                        }),
                        ExposeDecl::Resolver(ExposeResolverDecl {
                            source: ExposeSource::Child("netstack".to_string()),
                            source_name: "pkg".try_into().unwrap(),
                            target: ExposeTarget::Parent,
                            target_name: "pkg".try_into().unwrap(),
                        }),
                        ExposeDecl::Service(ExposeServiceDecl {
                            sources: vec![
                                ServiceSource::<ExposeServiceSource> {
                                    source: ExposeServiceSource::Child("netstack".to_string()),
                                    source_name: "netstack1".try_into().unwrap(),
                                },
                                ServiceSource::<ExposeServiceSource> {
                                    source: ExposeServiceSource::Child("netstack".to_string()),
                                    source_name: "netstack2".try_into().unwrap(),
                                },
                            ],
                            target_name: "mynetstack".try_into().unwrap(),
                            target: ExposeTarget::Parent,
                        }),
                    ],
                    offers: vec![
                        OfferDecl::Protocol(OfferProtocolDecl {
                            source: OfferServiceSource::Parent,
                            source_path: "/svc/legacy_netstack".try_into().unwrap(),
                            target: OfferTarget::Child("echo".to_string()),
                            target_path: "/svc/legacy_mynetstack".try_into().unwrap(),
                            dependency_type: DependencyType::WeakForMigration,
                        }),
                        OfferDecl::Directory(OfferDirectoryDecl {
                            source: OfferDirectorySource::Parent,
                            source_path: "/data/dir".try_into().unwrap(),
                            target: OfferTarget::Collection("modular".to_string()),
                            target_path: "/data".try_into().unwrap(),
                            rights: Some(fio2::Operations::Connect),
                            subdir: None,
                            dependency_type: DependencyType::Strong,
                        }),
                        OfferDecl::Storage(OfferStorageDecl::Cache(
                            OfferStorage {
                                source: OfferStorageSource::Storage("memfs".to_string()),
                                target: OfferTarget::Collection("modular".to_string()),
                            }
                        )),
                        OfferDecl::Runner(OfferRunnerDecl {
                            source: OfferRunnerSource::Parent,
                            source_name: "elf".try_into().unwrap(),
                            target: OfferTarget::Child("echo".to_string()),
                            target_name: "elf2".try_into().unwrap(),
                        }),
                        OfferDecl::Resolver(OfferResolverDecl {
                            source: OfferResolverSource::Parent,
                            source_name: "pkg".try_into().unwrap(),
                            target: OfferTarget::Child("echo".to_string()),
                            target_name: "pkg".try_into().unwrap(),
                        }),
                        OfferDecl::Event(OfferEventDecl {
                            source: OfferEventSource::Parent,
                            source_name: "started".into(),
                            target: OfferTarget::Child("echo".to_string()),
                            target_name: "mystarted".into(),
                            filter: Some(hashmap!{"path".to_string() => DictionaryValue::Str("/a".to_string())}),
                        }),
                        OfferDecl::Service(OfferServiceDecl {
                            sources: vec![
                                ServiceSource::<OfferServiceSource> {
                                    source: OfferServiceSource::Parent,
                                    source_name: "netstack1".try_into().unwrap(),
                                },
                                ServiceSource::<OfferServiceSource> {
                                    source: OfferServiceSource::Parent,
                                    source_name: "netstack2".try_into().unwrap(),
                                },
                            ],
                            target: OfferTarget::Child("echo".to_string()),
                            target_name: "mynetstack".try_into().unwrap(),
                        }),
                    ],
                    capabilities: vec![
                        CapabilityDecl::Service(ServiceDecl {
                            name: "netstack".into(),
                            source_path: "/netstack".try_into().unwrap(),
                        }),
                        CapabilityDecl::Protocol(ProtocolDecl {
                            name: "netstack2".into(),
                            source_path: "/netstack2".try_into().unwrap(),
                        }),
                        CapabilityDecl::Directory(DirectoryDecl {
                            name: "data".into(),
                            source_path: "/data".try_into().unwrap(),
                            rights: fio2::Operations::Connect,
                        }),
                        CapabilityDecl::Storage(StorageDecl {
                            name: "memfs".to_string(),
                            source_path: "data".try_into().unwrap(),
                            source: StorageDirectorySource::Parent,
                        }),
                        CapabilityDecl::Storage(StorageDecl {
                            name: "memfs2".to_string(),
                            source_path: "/data".try_into().unwrap(),
                            source: StorageDirectorySource::Parent,
                        }),
                        CapabilityDecl::Runner(RunnerDecl {
                            name: "elf".into(),
                            source: RunnerSource::Self_,
                            source_path: "/elf".try_into().unwrap(),
                        }),
                        CapabilityDecl::Resolver(ResolverDecl {
                            name: "pkg".into(),
                            source_path: "/pkg_resolver".try_into().unwrap(),
                        }),
                    ],
                    children: vec![
                        ChildDecl {
                            name: "netstack".to_string(),
                            url: "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cm".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                        },
                        ChildDecl {
                            name: "gtest".to_string(),
                            url: "fuchsia-pkg://fuchsia.com/gtest#meta/gtest.cm".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                        },
                        ChildDecl {
                            name: "echo".to_string(),
                            url: "fuchsia-pkg://fuchsia.com/echo#meta/echo.cm".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: Some("test_env".to_string()),
                        },
                    ],
                    collections: vec![
                        CollectionDecl {
                            name: "modular".to_string(),
                            durability: fsys::Durability::Persistent,
                            environment: None,
                        },
                        CollectionDecl {
                            name: "tests".to_string(),
                            durability: fsys::Durability::Transient,
                            environment: Some("test_env".to_string()),
                        },
                    ],
                    facets: Some(fsys::Object{entries: vec![
                       fsys::Entry{
                           key: "author".to_string(),
                           value: Some(Box::new(fsys::Value::Str("Fuchsia".to_string()))),
                       },
                    ]}),
                    environments: vec![
                        EnvironmentDecl {
                            name: "test_env".into(),
                            extends: fsys::EnvironmentExtends::Realm,
                            runners: vec![
                                RunnerRegistration {
                                    source_name: "runner".into(),
                                    source: RegistrationSource::Child("gtest".to_string()),
                                    target_name: "gtest-runner".into(),
                                }
                            ],
                            resolvers: vec![
                                ResolverRegistration {
                                    resolver: "pkg_resolver".into(),
                                    source: RegistrationSource::Parent,
                                    scheme: "fuchsia-pkg".to_string(),
                                }
                            ],
                            stop_timeout_ms: Some(4567),
                        }
                    ]
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
                Some(fsys::Ref::Parent(fsys::ParentRef {})),
                Some(fsys::Ref::Self_(fsys::SelfRef {})),
                Some(fsys::Ref::Child(fsys::ChildRef {
                    name: "foo".to_string(),
                    collection: None,
                })),
            ],
            input_type = Option<fsys::Ref>,
            result = vec![
                OfferServiceSource::Parent,
                OfferServiceSource::Self_,
                OfferServiceSource::Child("foo".to_string()),
            ],
            result_type = OfferServiceSource,
        },
        fidl_into_and_from_offer_event_source => {
            input = vec![Some(fsys::Ref::Parent(fsys::ParentRef {}))],
            input_type = Option<fsys::Ref>,
            result = vec![OfferEventSource::Parent],
            result_type = OfferEventSource,
        },
        fidl_into_and_from_offer_directory_source => {
            input = vec![
                Some(fsys::Ref::Parent(fsys::ParentRef {})),
                Some(fsys::Ref::Self_(fsys::SelfRef {})),
                Some(fsys::Ref::Framework(fsys::FrameworkRef {})),
                Some(fsys::Ref::Child(fsys::ChildRef {
                    name: "foo".to_string(),
                    collection: None,
                })),
            ],
            input_type = Option<fsys::Ref>,
            result = vec![
                OfferDirectorySource::Parent,
                OfferDirectorySource::Self_,
                OfferDirectorySource::Framework,
                OfferDirectorySource::Child("foo".to_string()),
            ],
            result_type = OfferDirectorySource,
        },
        fidl_into_and_from_offer_storage_source => {
            input = vec![
                Some(fsys::Ref::Parent(fsys::ParentRef {})),
                Some(fsys::Ref::Storage(fsys::StorageRef {
                    name: "foo".to_string(),
                })),
            ],
            input_type = Option<fsys::Ref>,
            result = vec![
                OfferStorageSource::Parent,
                OfferStorageSource::Storage("foo".to_string()),
            ],
            result_type = OfferStorageSource,
        },
        fidl_into_and_from_storage_capability => {
            input = vec![
                fsys::StorageDecl {
                    name: Some("minfs".to_string()),
                    source_path: Some("/minfs".to_string()),
                    source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                },
                fsys::StorageDecl {
                    name: Some("minfs".to_string()),
                    source_path: Some("minfs".to_string()),
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
                    source_path: CapabilityNameOrPath::try_from("/minfs").unwrap(),
                    source: StorageDirectorySource::Parent,
                },
                StorageDecl {
                    name: "minfs".to_string(),
                    source_path: CapabilityNameOrPath::try_from("minfs").unwrap(),
                    source: StorageDirectorySource::Child("foo".to_string()),
                },
            ],
            result_type = StorageDecl,
        },
    }

    test_fidl_into! {
        fidl_into_object => {
            input = {
                let obj_inner = fsys::Object{entries: vec![
                    fsys::Entry{
                        key: "string".to_string(),
                        value: Some(Box::new(fsys::Value::Str("bar".to_string()))),
                    },
                ]};
                let vector = fsys::Vector{values: vec![
                    Some(Box::new(fsys::Value::Obj(obj_inner))),
                    Some(Box::new(fsys::Value::Inum(-42)))
                ]};
                let obj_outer = fsys::Object{entries: vec![
                    fsys::Entry{
                        key: "array".to_string(),
                        value: Some(Box::new(fsys::Value::Vec(vector))),
                    },
                ]};
                let obj = fsys::Object {entries: vec![
                    fsys::Entry {
                        key: "bool".to_string(),
                        value: Some(Box::new(fsys::Value::Bit(true))),
                    },
                    fsys::Entry {
                        key: "obj".to_string(),
                        value: Some(Box::new(fsys::Value::Obj(obj_outer))),
                    },
                    fsys::Entry {
                        key: "float".to_string(),
                        value: Some(Box::new(fsys::Value::Fnum(3.14))),
                    },
                    fsys::Entry {
                        key: "int".to_string(),
                        value: Some(Box::new(fsys::Value::Inum(-42))),
                    },
                    fsys::Entry {
                        key: "null".to_string(),
                        value: None,
                    },
                    fsys::Entry {
                        key: "string".to_string(),
                        value: Some(Box::new(fsys::Value::Str("bar".to_string()))),
                    },
                ]};
                Some(obj)
            },
            result = {
                let mut obj_inner = HashMap::new();
                obj_inner.insert("string".to_string(), Value::Str("bar".to_string()));
                let mut obj_outer = HashMap::new();
                let vector = vec![Value::Obj(obj_inner), Value::Inum(-42)];
                obj_outer.insert("array".to_string(), Value::Vec(vector));

                let mut obj: HashMap<String, Value> = HashMap::new();
                obj.insert("bool".to_string(), Value::Bit(true));
                obj.insert("float".to_string(), Value::Fnum(3.14));
                obj.insert("int".to_string(), Value::Inum(-42));
                obj.insert("string".to_string(), Value::Str("bar".to_string()));
                obj.insert("obj".to_string(), Value::Obj(obj_outer));
                obj.insert("null".to_string(), Value::Null);
                Some(obj)
            },
        },
    }
}
