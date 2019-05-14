use serde_derive::{Deserialize, Serialize};
use serde_json::{Map, Value};

pub const SERVICE: &str = "service";
pub const DIRECTORY: &str = "directory";
pub const LAZY: &str = "lazy";
pub const EAGER: &str = "eager";

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
    pub facets: Option<Map<String, Value>>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Child {
    pub name: String,
    pub url: String,
    pub startup: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum Use {
    #[serde(rename = "service")]
    Service(UseService),
    #[serde(rename = "directory")]
    Directory(UseDirectory),
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
pub enum Expose {
    #[serde(rename = "service")]
    Service(ExposeService),
    #[serde(rename = "directory")]
    Directory(ExposeDirectory),
}

#[derive(Serialize, Deserialize, Debug)]
pub struct ExposeService {
    pub source: ExposeSource,
    pub source_path: String,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct ExposeDirectory {
    pub source: ExposeSource,
    pub source_path: String,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum Offer {
    #[serde(rename = "service")]
    Service(OfferService),
    #[serde(rename = "directory")]
    Directory(OfferDirectory),
}

#[derive(Serialize, Deserialize, Debug)]
pub struct OfferService {
    pub source: OfferSource,
    pub source_path: String,
    pub targets: Vec<Target>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct OfferDirectory {
    pub source: OfferSource,
    pub source_path: String,
    pub targets: Vec<Target>,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum ExposeSource {
    #[serde(rename = "myself")]
    Myself(SelfId),
    #[serde(rename = "child")]
    Child(ChildId),
}

#[derive(Serialize, Deserialize, Debug)]
pub enum OfferSource {
    #[serde(rename = "realm")]
    Realm(RealmId),
    #[serde(rename = "myself")]
    Myself(SelfId),
    #[serde(rename = "child")]
    Child(ChildId),
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Target {
    pub target_path: String,
    pub child_name: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct RealmId {}

#[derive(Serialize, Deserialize, Debug)]
pub struct SelfId {}

#[derive(Serialize, Deserialize, Debug)]
pub struct ChildId {
    pub name: String,
}
