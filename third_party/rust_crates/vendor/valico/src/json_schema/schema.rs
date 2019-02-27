use url;
use std::collections;
use serde_json::{Value};
use phf;
use std::ops;

use super::helpers;
use super::scope;
use super::keywords;
use super::validators;

#[derive(Debug)]
pub struct WalkContext<'a> {
    pub url: &'a url::Url,
    pub fragment: Vec<String>,
    pub scopes: &'a mut collections::HashMap<String, Vec<String>>
}

impl<'a> WalkContext<'a> {
    pub fn escaped_fragment(&self) -> String {
        helpers::connect(self.fragment.iter().map(|s| s.as_ref()).collect::<Vec<&str>>().as_ref())
    }
}

#[derive(Debug)]
#[allow(missing_copy_implementations)]
pub enum SchemaError {
    WrongId,
    IdConflicts,
    NotAnObject,
    UrlParseError(url::ParseError),
    UnknownKey(String),
    Malformed {
        path: String,
        detail: String
    }
}

#[derive(Debug)]
pub struct ScopedSchema<'a> {
    scope: &'a scope::Scope,
    schema: &'a Schema
}

impl<'a> ops::Deref for ScopedSchema<'a> {
    type Target = Schema;

    fn deref(&self) -> &Schema {
        &self.schema
    }
}

impl<'a> ScopedSchema<'a> {
    pub fn new(scope: &'a scope::Scope, schema: &'a Schema) -> ScopedSchema<'a> {
        ScopedSchema {
            scope: scope,
            schema: schema
        }
    }

    pub fn validate(&self, data: &Value) -> validators::ValidationState {
        return self.schema.validate_in_scope(data, "", self.scope);
    }

    pub fn validate_in(&self, data: &Value, path: &str) -> validators::ValidationState {
        return self.schema.validate_in_scope(data, path, self.scope);
    }
}

#[derive(Debug)]
#[allow(dead_code)]
pub struct Schema {
    pub id: Option<url::Url>,
    schema: Option<url::Url>,
    original: Value,
    tree: collections::BTreeMap<String, Schema>,
    validators: validators::Validators,
    scopes: collections::HashMap<String, Vec<String>>
}

include!(concat!(env!("OUT_DIR"), "/codegen.rs"));

pub struct CompilationSettings<'a> {
    pub keywords: &'a keywords::KeywordMap,
    pub ban_unknown_keywords: bool
}

impl<'a> CompilationSettings<'a> {
    pub fn new(keywords: &'a keywords::KeywordMap, ban_unknown_keywords: bool) -> CompilationSettings<'a> {
        CompilationSettings {
            keywords: keywords,
            ban_unknown_keywords: ban_unknown_keywords,
        }
    }
}

impl Schema {
    fn compile(def: Value, external_id: Option<url::Url>, settings: CompilationSettings) -> Result<Schema, SchemaError> {
        if !def.is_object() {
            return Err(SchemaError::NotAnObject)
        }

        let id = if external_id.is_some() {
            external_id.unwrap()
        } else {
            try!(helpers::parse_url_key("id", &def)).clone().unwrap_or_else(|| helpers::generate_id())
        };

        let schema = try!(helpers::parse_url_key("$schema", &def));

        let (tree, mut scopes) = {
            let mut tree = collections::BTreeMap::new();
            let obj = def.as_object().unwrap();

            let mut scopes = collections::HashMap::new();

            for (key, value) in obj.iter() {
                if !value.is_object() && !value.is_array() { continue; }
                if FINAL_KEYS.contains(&key[..]) { continue; }

                let mut context = WalkContext {
                    url: &id,
                    fragment: vec![key.clone()],
                    scopes: &mut scopes
                };

                let scheme = try!(Schema::compile_sub(
                    value.clone(),
                    &mut context,
                    &settings,
                    !NON_SCHEMA_KEYS.contains(&key[..])
                ));

                tree.insert(helpers::encode(key), scheme);
            }

            (tree, scopes)
        };

        let validators = try!(Schema::compile_keywords(&def, &WalkContext {
            url: &id,
            fragment: vec![],
            scopes: &mut scopes,
        }, &settings));

        let schema = Schema {
            id: Some(id),
            schema: schema,
            original: def,
            tree: tree,
            validators: validators,
            scopes: scopes
        };

        Ok(schema)
    }

    fn compile_keywords(def: &Value, context: &WalkContext, settings: &CompilationSettings) -> Result<validators::Validators, SchemaError> {
        let mut validators = vec![];
        let mut keys: collections::HashSet<&str> = def.as_object().unwrap().keys().map(|key| key.as_ref()).collect();
        let mut not_consumed = collections::HashSet::new();

        loop {
            let key = keys.iter().next().cloned();
            if key.is_some() {
                let key = key.unwrap();
                match settings.keywords.get(&key) {
                    Some(keyword) => {
                        keyword.consume(&mut keys);

                        let is_exclusive_keyword = keyword.keyword.is_exclusive();

                        match try!(keyword.keyword.compile(def, context)) {
                            Some(validator) => {
                                if is_exclusive_keyword {
                                    validators = vec![validator];
                                } else {
                                    validators.push(validator);
                                }
                            },
                            None => ()
                        };

                        if is_exclusive_keyword {
                            break;
                        }
                    },
                    None => {
                        keys.remove(&key);
                        if settings.ban_unknown_keywords {
                            not_consumed.insert(key);
                        }
                    }
                }
            } else {
                break;
            }
        }

        if settings.ban_unknown_keywords && not_consumed.len() > 0 {
            for key in not_consumed.iter() {
                if !ALLOW_NON_CONSUMED_KEYS.contains(&key[..]) {
                    return Err(SchemaError::UnknownKey(key.to_string()))
                }
            }
        }

        Ok(validators)
    }

    fn compile_sub(def: Value, context: &mut WalkContext, keywords: &CompilationSettings, is_schema: bool) -> Result<Schema, SchemaError> {

        let mut id = None;
        let mut schema = None;

        if is_schema {
            id = try!(helpers::parse_url_key_with_base("id", &def, context.url));
            schema = try!(helpers::parse_url_key("$schema", &def));
        }

        let tree = {
            let mut tree = collections::BTreeMap::new();

            if def.is_object() {
                let obj = def.as_object().unwrap();
                let parent_key = &context.fragment[context.fragment.len() - 1];

                for (key, value) in obj.iter() {
                    if !value.is_object() && !value.is_array() { continue; }
                    if !PROPERTY_KEYS.contains(&parent_key[..]) && FINAL_KEYS.contains(&key[..]) { continue; }

                    let mut current_fragment = context.fragment.clone();
                    current_fragment.push(key.clone());

                    let is_schema = PROPERTY_KEYS.contains(&parent_key[..]) || !NON_SCHEMA_KEYS.contains(&key[..]);

                    let mut context = WalkContext {
                        url: id.as_ref().unwrap_or(context.url),
                        fragment: current_fragment,
                        scopes: context.scopes
                    };

                    let scheme = try!(Schema::compile_sub(
                        value.clone(),
                        &mut context,
                        keywords,
                        is_schema
                    ));

                    tree.insert(helpers::encode(key), scheme);
                }
            } else if def.is_array() {
                let array = def.as_array().unwrap();

                for (idx, value) in array.iter().enumerate() {
                    if !value.is_object() && !value.is_array() { continue; }

                    let mut current_fragment = context.fragment.clone();
                    current_fragment.push(idx.to_string().clone());

                    let mut context = WalkContext {
                        url: id.as_ref().unwrap_or(context.url),
                        fragment: current_fragment,
                        scopes: context.scopes
                    };

                    let scheme = try!(Schema::compile_sub(
                        value.clone(),
                        &mut context,
                        keywords,
                        true
                    ));

                    tree.insert(idx.to_string().clone(), scheme);
                }
            }

            tree
        };

        if id.is_some() {
            context.scopes.insert(id.clone().unwrap().into_string(), context.fragment.clone());
        }

        let validators = if is_schema && def.is_object() {
            try!(Schema::compile_keywords(&def, context, keywords))
        } else {
            vec![]
        };

        let schema = Schema {
            id: id,
            schema: schema,
            original: def,
            tree: tree,
            validators: validators,
            scopes: collections::HashMap::new()
        };

        Ok(schema)
    }

    pub fn resolve(&self, id: &str) -> Option<&Schema> {
        let path = self.scopes.get(id);
        path.map(|path| {
            let mut schema = self;
            for item in path.iter() {
                schema = schema.tree.get(item).unwrap()
            }
            schema
        })
    }

    pub fn resolve_fragment(&self, fragment: &str) -> Option<&Schema> {
        assert!(fragment.starts_with("/"), "Can't resolve id fragments");

        let parts = fragment[1..].split("/");
        let mut schema = self;
        for part in parts {
            match schema.tree.get(part) {
                Some(sch) => schema = sch,
                None => return None
            }
        }

        Some(schema)
    }
}

impl Schema {
    fn validate_in_scope(&self, data: &Value, path: &str, scope: &scope::Scope) -> validators::ValidationState {
        let mut state = validators::ValidationState::new();

        for validator in self.validators.iter() {
            state.append(validator.validate(data, path, scope))
        }

        state
    }
}

pub fn compile(def: Value, external_id: Option<url::Url>, settings: CompilationSettings) -> Result<Schema, SchemaError> {
    Schema::compile(def, external_id, settings)
}

#[test]
fn schema_doesnt_compile_not_object() {
    assert!(Schema::compile(Value::Bool(true), None, CompilationSettings::new(&keywords::default(), true)).is_err());
}
