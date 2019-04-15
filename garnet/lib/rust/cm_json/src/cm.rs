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
    pub r#type: String,
    pub source_path: String,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Expose {
    pub r#type: String,
    pub source_path: String,
    pub source: Source,
    pub target_path: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Offer {
    pub r#type: String,
    pub source_path: String,
    pub source: Source,
    pub targets: Vec<Target>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Child {
    pub name: String,
    pub uri: String,
    pub startup: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Source {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub realm: Option<RealmId>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub myself: Option<SelfId>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub child: Option<ChildId>,
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
