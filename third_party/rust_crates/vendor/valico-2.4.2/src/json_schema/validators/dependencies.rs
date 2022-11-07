use std::collections;
use serde_json::{Value};
use url;

use super::super::errors;
use super::super::scope;

#[derive(Debug)]
pub enum DepKind {
    Schema(url::Url),
    Property(Vec<String>)
}

#[allow(missing_copy_implementations)]
pub struct Dependencies {
    pub items: collections::HashMap<String, DepKind>
}

impl super::Validator for Dependencies {
    fn validate(&self, object: &Value, path: &str, scope: &scope::Scope) -> super::ValidationState {
        if !object.is_object() {
            return super::ValidationState::new()
        }

        let mut state = super::ValidationState::new();

        for (key, dep) in self.items.iter() {
            if object.get(&key).is_some() {
                match dep {
                    &DepKind::Schema(ref url) => {
                        let schema = scope.resolve(url);
                        if schema.is_some() {
                            state.append(schema.unwrap().validate_in(object, path));
                        } else {
                            state.missing.push(url.clone())
                        }
                    },
                    &DepKind::Property(ref keys) => {
                        for key in keys.iter() {
                            if !object.get(&key).is_some() {
                                state.errors.push(Box::new(
                                    errors::Required {
                                        path: [path, key.as_ref()].join("/")
                                    }
                                ))
                            }
                        }
                    }
                }
            }
        }

        state
    }
}
