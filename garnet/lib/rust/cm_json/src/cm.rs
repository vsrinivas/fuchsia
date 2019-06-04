use serde_derive::{Deserialize, Serialize};
use serde_json::{Map, Value};

pub const SERVICE: &str = "service";
pub const DIRECTORY: &str = "directory";
pub const LAZY: &str = "lazy";
pub const EAGER: &str = "eager";
pub const PERSISTENT: &str = "persistent";
pub const TRANSIENT: &str = "transient";

#[derive(Serialize, Deserialize, Debug, Default)]
pub struct Document {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub program: Option<Map<String, Value>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub uses: Option<Vec<Use>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub exposes: Option<Vec<Expose>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub offers: Option<Vec<Offer>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub children: Option<Vec<Child>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub collections: Option<Vec<Collection>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub storage: Option<Vec<Storage>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub facets: Option<Map<String, Value>>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Child {
    pub name: String,
    pub url: String,
    pub startup: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Collection {
    pub name: String,
    pub durability: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Storage {
    pub name: String,
    pub source_path: String,
    pub source: Ref,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum Use {
    #[serde(rename = "service")]
    Service(UseService),
    #[serde(rename = "directory")]
    Directory(UseDirectory),
    #[serde(rename = "storage")]
    Storage(UseStorage),
}

#[derive(Serialize, Deserialize, Debug)]
pub struct UseService {
    pub source_path: String,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct UseDirectory {
    pub source_path: String,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct UseStorage {
    #[serde(rename = "type")]
    pub type_: StorageType,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum Expose {
    #[serde(rename = "service")]
    Service(ExposeService),
    #[serde(rename = "directory")]
    Directory(ExposeDirectory),
}

#[derive(Serialize, Deserialize, Debug)]
pub struct ExposeService {
    pub source: Ref,
    pub source_path: String,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct ExposeDirectory {
    pub source: Ref,
    pub source_path: String,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum Offer {
    #[serde(rename = "service")]
    Service(OfferService),
    #[serde(rename = "directory")]
    Directory(OfferDirectory),
    #[serde(rename = "storage")]
    Storage(OfferStorage),
}

#[derive(Serialize, Deserialize, Debug)]
pub struct OfferService {
    pub source: Ref,
    pub source_path: String,
    pub targets: Vec<Target>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct OfferDirectory {
    pub source: Ref,
    pub source_path: String,
    pub targets: Vec<Target>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct OfferStorage {
    #[serde(rename = "type")]
    pub type_: StorageType,
    pub source: Ref,
    pub dests: Vec<Ref>,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum StorageType {
    #[serde(rename = "data")]
    Data,
    #[serde(rename = "cache")]
    Cache,
    #[serde(rename = "meta")]
    Meta,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Target {
    pub target_path: String,
    pub dest: Ref,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum Ref {
    #[serde(rename = "realm")]
    Realm(RealmRef),
    #[serde(rename = "self")]
    Self_(SelfRef),
    #[serde(rename = "child")]
    Child(ChildRef),
    #[serde(rename = "collection")]
    Collection(CollectionRef),
    #[serde(rename = "storage")]
    Storage(StorageRef),
}

#[derive(Serialize, Deserialize, Debug)]
pub struct RealmRef {}

#[derive(Serialize, Deserialize, Debug)]
pub struct SelfRef {}

#[derive(Serialize, Deserialize, Debug)]
pub struct ChildRef {
    pub name: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct CollectionRef {
    pub name: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct StorageRef {
    pub name: String,
}
