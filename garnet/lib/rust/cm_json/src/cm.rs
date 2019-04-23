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
pub struct Use {
    pub capability: Capability,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Expose {
    pub capability: Capability,
    pub source: ExposeSource,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Offer {
    pub capability: Capability,
    pub source: OfferSource,
    pub targets: Vec<Target>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Child {
    pub name: String,
    pub uri: String,
    pub startup: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum Capability {
    #[serde(rename = "service")]
    Service(Service),
    #[serde(rename = "directory")]
    Directory(Directory),
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
pub struct Service {
    pub path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Directory {
    pub path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct RealmId {}

#[derive(Serialize, Deserialize, Debug)]
pub struct SelfId {}

#[derive(Serialize, Deserialize, Debug)]
pub struct ChildId {
    pub name: String,
}
