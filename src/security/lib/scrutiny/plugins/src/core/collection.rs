// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{
        util::{
            jsons::{deserialize_services, deserialize_url, serialize_services, serialize_url},
            types::Protocol as ProtocolName,
        },
        DataCollection,
    },
    core::slice::Iter,
    fuchsia_merkle::Hash,
    fuchsia_url::{AbsoluteComponentUrl, PackageName, PackageVariant},
    scrutiny_utils::zbi::ZbiSection,
    serde::{Deserialize, Serialize},
    std::{
        cmp::{Ord, Ordering, PartialOrd},
        collections::{HashMap, HashSet},
        path::PathBuf,
    },
    url::Url,
};

/// Captures metadata about where a component was loaded from.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub enum ComponentSource {
    /// Component manifest was not found, but the component was designated as a
    /// service provider in the service mappings in a Component Framework v1
    /// sysmgr config file.
    Inferred,
    /// Component was loaded ZBI bootfs.
    ZbiBootfs,
    /// Component was loaded from a package with the given merkle hash.
    Package(Hash),
    /// Component was loaded from a package with the given merkle hash. The
    /// package is listed in the static packages index.
    StaticPackage(Hash),
}

/// Defines a component. Each component has a unique id which is used to link
/// it in the Route table. Each component also has a url and a version. This
/// structure is intended to be lightweight and general purpose if you need to
/// append additional information about a component make another table and
/// index it on the `component.id`.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Component {
    pub id: i32,
    #[serde(serialize_with = "serialize_url", deserialize_with = "deserialize_url")]
    pub url: Url,
    pub version: i32,
    pub source: ComponentSource,
}

#[derive(Default, Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Components {
    pub entries: Vec<Component>,
}

impl Components {
    pub fn new(entries: Vec<Component>) -> Self {
        Self { entries }
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn iter(&self) -> Iter<'_, Component> {
        self.entries.iter()
    }

    pub fn push(&mut self, value: Component) {
        self.entries.push(value)
    }
}

impl DataCollection for Components {
    fn collection_name() -> String {
        "Components Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains all the components found in all packages".to_string()
    }
}

/// Defines a fuchsia package. Each package has a unique url. This provides an
/// expanded meta/contents so you can see all of the files defined in this
/// package.
#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct Package {
    /// The name of the package as would be designated as `[name]` in URLs such
    /// as `fuchsia-pkg://[host]/[name]`,
    /// `fuchsia-pkg://[host]/[name]/[variant]`, or
    /// `fuchsia-pkg://[host]/[name]/[variant]?hash=[hash]`.
    pub name: PackageName,
    /// The variant of the package as would be desiganted as `[variant]` in URLs
    /// such as `fuchsia-pkg://[host]/[name]/[variant]`, or
    /// `fuchsia-pkg://[host]/[name]/[variant]?hash=[hash]`.
    pub variant: Option<PackageVariant>,
    /// The merkle root hash of the package meta.far file as would be designated
    /// as `[hash]` in URLs such as
    /// `fuchsia-pkg://[host]/[name]/[variant]?hash=[hash]`.
    pub merkle: Hash,
    /// A mapping from internal package paths to merkle root hashes of content
    /// (that is non-meta) files designated in the package meta.far.
    pub contents: HashMap<PathBuf, Hash>,
    /// A mapping from internal package meta paths to meta file contents.
    pub meta: HashMap<PathBuf, Vec<u8>>,
}

// Define a zero-copy type that encapsulates "URL part" of `Package` and use it
// for ordering `Package` instances.
#[derive(Eq, Ord, PartialEq, PartialOrd)]
struct PackageUrlPart<'a> {
    name: &'a PackageName,
    variant: &'a Option<PackageVariant>,
    merkle: &'a Hash,
}

impl<'a> From<&'a Package> for PackageUrlPart<'a> {
    fn from(package: &'a Package) -> Self {
        PackageUrlPart { name: &package.name, variant: &package.variant, merkle: &package.merkle }
    }
}

impl PartialOrd<Package> for Package {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        PackageUrlPart::from(self).partial_cmp(&PackageUrlPart::from(other))
    }
}

impl Ord for Package {
    fn cmp(&self, other: &Self) -> Ordering {
        PackageUrlPart::from(self).cmp(&PackageUrlPart::from(other))
    }
}

#[derive(Default, Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Packages {
    pub entries: Vec<Package>,
}

impl Packages {
    pub fn new(entries: Vec<Package>) -> Self {
        Self { entries }
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn iter(&self) -> Iter<'_, Package> {
        self.entries.iter()
    }

    pub fn push(&mut self, value: Package) {
        self.entries.push(value)
    }
}

impl DataCollection for Packages {
    fn collection_name() -> String {
        "Packages Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains all the packages found in the build".to_string()
    }
}

/// A component instance is a specific instantiation of a component. These
/// may run in a particular realm with certain restrictions.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct ComponentInstance {
    pub id: i32,
    pub moniker: String,
    pub component_id: i32,
}

#[derive(Default, Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct ComponentInstances {
    pub entries: Vec<ComponentInstance>,
}

impl ComponentInstances {
    pub fn new(entries: Vec<ComponentInstance>) -> Self {
        Self { entries }
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn iter(&self) -> Iter<'_, ComponentInstance> {
        self.entries.iter()
    }

    pub fn push(&mut self, value: ComponentInstance) {
        self.entries.push(value)
    }
}

impl DataCollection for ComponentInstances {
    fn collection_name() -> String {
        "Component Instance Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains all the v1 instances of components found in the build".to_string()
    }
}

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq, Hash)]
pub enum Capability {
    Service(ServiceCapability),
    Protocol(ProtocolCapability),
    Directory(DirectoryCapability),
    Storage(StorageCapability),
    Runner(RunnerCapability),
    Resolver(ResolverCapability),
    Event(EventCapability),
    EventStream(EventStreamCapability),
}

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq, Hash)]
pub struct ServiceCapability {
    pub source_name: String,
}

impl ServiceCapability {
    pub fn new(source_name: String) -> Self {
        Self { source_name }
    }
}

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq, Hash)]
pub struct ProtocolCapability {
    pub source_name: String,
}

impl ProtocolCapability {
    pub fn new(source_name: String) -> Self {
        Self { source_name }
    }
}

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq, Hash)]
pub struct DirectoryCapability {
    pub source_name: String,
}

impl DirectoryCapability {
    pub fn new(source_name: String) -> Self {
        Self { source_name }
    }
}

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq, Hash)]
pub struct StorageCapability {
    pub source_name: String,
}

impl StorageCapability {
    pub fn new(source_name: String) -> Self {
        Self { source_name }
    }
}

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq, Hash)]
pub struct RunnerCapability {
    pub source_name: String,
}

impl RunnerCapability {
    pub fn new(source_name: String) -> Self {
        Self { source_name }
    }
}

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq, Hash)]
pub struct ResolverCapability {
    pub source_name: String,
}

impl ResolverCapability {
    pub fn new(source_name: String) -> Self {
        Self { source_name }
    }
}

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq, Hash)]
pub struct EventCapability {
    pub source_name: String,
}

impl EventCapability {
    pub fn new(source_name: String) -> Self {
        Self { source_name }
    }
}

#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq, Hash)]
pub struct EventStreamCapability {
    pub source_name: String,
}

impl EventStreamCapability {
    pub fn new(source_name: String) -> Self {
        Self { source_name }
    }
}

/// Defines the manifest data in terms of the component framework version it
/// represents.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub enum ManifestData {
    Version1(String),
    Version2 { cm_base64: String, cvf_bytes: Option<Vec<u8>> },
}

/// Defines a component manifest. The `component_id` maps 1:1 to
/// `component.id` indexes. This is stored in a different table as most queries
/// don't need the raw manifest.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Manifest {
    pub component_id: i32,
    pub manifest: ManifestData,
    pub uses: Vec<Capability>,
}

#[derive(Default, Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Manifests {
    pub entries: Vec<Manifest>,
}

impl Manifests {
    pub fn new(entries: Vec<Manifest>) -> Self {
        Self { entries }
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn iter(&self) -> Iter<'_, Manifest> {
        self.entries.iter()
    }

    pub fn push(&mut self, value: Manifest) {
        self.entries.push(value)
    }
}

impl DataCollection for Manifests {
    fn collection_name() -> String {
        "Manifest Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains all the v1 & v2 manifests found in the build".to_string()
    }
}

// TODO(benwright) - Add support for "first class" capabilities such as runners,
// resolvers and events.
/// Defines a link between two components. The `src_id` is the `component_instance.id`
/// of the component giving a service or directory to the `dst_id`. The
/// `protocol_id` refers to the Protocol with this link.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Route {
    pub id: i32,
    pub src_id: i32,
    pub dst_id: i32,
    pub service_name: String,
    pub protocol_id: i32,
}

#[derive(Default, Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Routes {
    pub entries: Vec<Route>,
}

impl Routes {
    pub fn new(entries: Vec<Route>) -> Self {
        Self { entries }
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn iter(&self) -> Iter<'_, Route> {
        self.entries.iter()
    }

    pub fn push(&mut self, value: Route) {
        self.entries.push(value)
    }
}

impl DataCollection for Routes {
    fn collection_name() -> String {
        "Routes v1 Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains all the v1 component routes found in the build".to_string()
    }
}

/// Defines either a FIDL or Directory protocol with some interface name such
/// as fuchshia.foo.Bar and an optional path such as "/dev".
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Protocol {
    pub id: i32,
    pub interface: String,
    pub path: String,
}

#[derive(Default, Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Protocols {
    pub entries: Vec<Protocol>,
}

impl Protocols {
    pub fn new(entries: Vec<Protocol>) -> Self {
        Self { entries }
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn iter(&self) -> Iter<'_, Protocol> {
        self.entries.iter()
    }

    pub fn push(&mut self, value: Protocol) {
        self.entries.push(value)
    }
}

impl DataCollection for Protocols {
    fn collection_name() -> String {
        "Protocols v1 Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains all the v1 protocols found in the build".to_string()
    }
}

/// Defines all of the parsed information in the ZBI.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Zbi {
    // Raw section data for each zbi section. This section isn't serialized to
    // disk because it occupies a large amount of space.
    #[serde(skip)]
    pub sections: Vec<ZbiSection>,
    // File names to data contained in bootfs.
    // TODO(benwright) - Work out how to optimize this for speed.
    #[serde(skip)]
    pub bootfs: HashMap<String, Vec<u8>>,
    pub cmdline: String,
}

impl DataCollection for Zbi {
    fn collection_name() -> String {
        "ZBI  Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains all the items found in the zircon boot image (ZBI) in the update package"
            .to_string()
    }
}

/// Defines all the services exposed by sysmgr.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Sysmgr {
    /// Mapping from service-name -> url.
    #[serde(serialize_with = "serialize_services", deserialize_with = "deserialize_services")]
    pub services: HashMap<ProtocolName, Url>,
    /// Url of sys realm apps, started when the sys realm starts
    pub apps: HashSet<AbsoluteComponentUrl>,
}

impl Sysmgr {
    pub fn new(services: HashMap<ProtocolName, Url>, apps: HashSet<AbsoluteComponentUrl>) -> Self {
        Self { services, apps }
    }

    pub fn iter(&self) -> std::collections::hash_map::Iter<'_, ProtocolName, Url> {
        self.services.iter()
    }
}

impl DataCollection for Sysmgr {
    fn collection_name() -> String {
        "Sysmgr Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains all the service and app mappings found in the sysmgr config".to_string()
    }
}

/// Defines the set of files touched by core plugin data collection. This set
/// can be important when integrating with tooling that demands a complete set
/// of dependencies during tool execution.
#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct CoreDataDeps {
    pub deps: HashSet<PathBuf>,
}

impl CoreDataDeps {
    pub fn new(deps: HashSet<PathBuf>) -> Self {
        Self { deps }
    }
}

impl DataCollection for CoreDataDeps {
    fn collection_name() -> String {
        "Core Data Dependencies".to_string()
    }
    fn collection_description() -> String {
        "Contains a set of paths core data collection read from".to_string()
    }
}

#[cfg(test)]
pub mod testing {
    use {super::ComponentSource, fuchsia_merkle::HASH_SIZE};

    const FAKE_PKG_MERKLE: [u8; HASH_SIZE] = [0x42; HASH_SIZE];

    pub fn fake_component_src_pkg() -> ComponentSource {
        ComponentSource::Package(FAKE_PKG_MERKLE.into())
    }
}
