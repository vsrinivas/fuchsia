use serde_json::{Value};
use std::fmt;
use std::rc;
use std::collections;
use std::any;

use super::schema;
use super::validators;

pub type KeywordResult = Result<Option<validators::BoxedValidator>, schema::SchemaError>;
pub type KeywordPair = (Vec<&'static str>, Box<Keyword + 'static>);
pub type KeywordPairs = Vec<KeywordPair>;
pub type KeywordMap = collections::HashMap<&'static str, rc::Rc<KeywordConsumer>>;

pub trait Keyword: Sync + any::Any {
    fn compile(&self, &Value, &schema::WalkContext) -> KeywordResult;

    fn is_exclusive(&self) -> bool {
        false
    }
}

impl<T: 'static + Send + Sync + any::Any> Keyword for T where T: Fn(&Value, &schema::WalkContext) -> KeywordResult {
    fn compile(&self, def: &Value, ctx: &schema::WalkContext) -> KeywordResult {
        self(def, ctx)
    }
}

impl fmt::Debug for Keyword + 'static {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        fmt.write_str("<keyword>")
    }
}

macro_rules! keyword_key_exists {
    ($val:expr, $key:expr) => {{
        let maybe_val = $val.get($key);
        if maybe_val.is_none() {
            return Ok(None)
        } else {
           maybe_val.unwrap()
        }
    }}
}

pub mod multiple_of;
pub mod maxmin;
#[macro_use]
pub mod maxmin_length;
pub mod maxmin_items;
pub mod pattern;
pub mod unique_items;
pub mod items;
pub mod maxmin_properties;
pub mod required;
pub mod properties;
pub mod dependencies;
pub mod enum_;
pub mod type_;
pub mod of;
pub mod ref_;
pub mod not;
pub mod format;

pub fn default() -> KeywordMap {
    let mut map = collections::HashMap::new();

    decouple_keyword((vec!["multipleOf"], Box::new(multiple_of::MultipleOf)), &mut map);
    decouple_keyword((vec!["maximum", "exclusiveMaximum"], Box::new(maxmin::Maximum)), &mut map);
    decouple_keyword((vec!["minimum", "exclusiveMinimum"], Box::new(maxmin::Minimum)), &mut map);
    decouple_keyword((vec!["maxLength"], Box::new(maxmin_length::MaxLength)), &mut map);
    decouple_keyword((vec!["minLength"], Box::new(maxmin_length::MinLength)), &mut map);
    decouple_keyword((vec!["pattern"], Box::new(pattern::Pattern)), &mut map);
    decouple_keyword((vec!["maxItems"], Box::new(maxmin_items::MaxItems)), &mut map);
    decouple_keyword((vec!["minItems"], Box::new(maxmin_items::MinItems)), &mut map);
    decouple_keyword((vec!["uniqueItems"], Box::new(unique_items::UniqueItems)), &mut map);
    decouple_keyword((vec!["items", "additionalItems"], Box::new(items::Items)), &mut map);
    decouple_keyword((vec!["maxProperties"], Box::new(maxmin_properties::MaxProperties)), &mut map);
    decouple_keyword((vec!["minProperties"], Box::new(maxmin_properties::MinProperties)), &mut map);
    decouple_keyword((vec!["required"], Box::new(required::Required)), &mut map);
    decouple_keyword((vec!["properties", "additionalProperties", "patternProperties"], Box::new(properties::Properties)), &mut map);
    decouple_keyword((vec!["dependencies"], Box::new(dependencies::Dependencies)), &mut map);
    decouple_keyword((vec!["enum"], Box::new(enum_::Enum)), &mut map);
    decouple_keyword((vec!["type"], Box::new(type_::Type)), &mut map);
    decouple_keyword((vec!["allOf"], Box::new(of::AllOf)), &mut map);
    decouple_keyword((vec!["anyOf"], Box::new(of::AnyOf)), &mut map);
    decouple_keyword((vec!["oneOf"], Box::new(of::OneOf)), &mut map);
    decouple_keyword((vec!["$ref"], Box::new(ref_::Ref)), &mut map);
    decouple_keyword((vec!["not"], Box::new(not::Not)), &mut map);

    map
}

#[derive(Debug)]
pub struct KeywordConsumer {
    pub keys: Vec<&'static str>,
    pub keyword: Box<Keyword + 'static>
}

impl KeywordConsumer {
    pub fn consume(&self, set: &mut collections::HashSet<&str>) {
        for key in self.keys.iter() {
            if set.contains(key) {
                set.remove(key);
            }
        }
    }
}

pub fn decouple_keyword(keyword_pair: KeywordPair,
                        map: &mut KeywordMap) {
    let (keys, keyword) = keyword_pair;
    let consumer = rc::Rc::new(KeywordConsumer { keys: keys.clone(), keyword: keyword });
    for key in keys.iter() {
        map.insert(key, consumer.clone());
    }
}
