use serde_json::{Value};
use std::cmp;
use url;

use super::super::errors;
use super::super::scope;

#[derive(Debug)]
pub enum ItemsKind {
    Schema(url::Url),
    Array(Vec<url::Url>)
}

#[derive(Debug)]
pub enum AdditionalKind {
    Boolean(bool),
    Schema(url::Url)
}

#[allow(missing_copy_implementations)]
pub struct Items {
    pub items: Option<ItemsKind>,
    pub additional: Option<AdditionalKind>
}

impl super::Validator for Items {
    fn validate(&self, val: &Value, path: &str, scope: &scope::Scope) -> super::ValidationState {
        let array = nonstrict_process!(val.as_array(), path);

        let mut state = super::ValidationState::new();

        match self.items {
            Some(ItemsKind::Schema(ref url)) => {
                // Just validate all items against the schema

                let schema = scope.resolve(url);
                if schema.is_some() {
                    let schema = schema.unwrap();
                    for (idx, item) in array.iter().enumerate() {
                        let item_path = [path, idx.to_string().as_ref()].join("/");
                        state.append(schema.validate_in(item, item_path.as_ref()));
                    }
                } else {
                    state.missing.push(url.clone());
                }
            },
            Some(ItemsKind::Array(ref urls)) => {
                let min = cmp::min(urls.len(), array.len());

                // Validate against schemas
                for idx in 0..min {
                    let schema = scope.resolve(&urls[idx]);
                    let item = &array[idx];

                    if schema.is_some() {
                        let item_path = [path, idx.to_string().as_ref()].join("/");
                        state.append(schema.unwrap().validate_in(item, item_path.as_ref()))
                    } else {
                        state.missing.push(urls[idx].clone())
                    }
                }

                // Validate agains additional items
                if array.len() > urls.len() {
                    match self.additional {
                        Some(AdditionalKind::Boolean(allow)) if allow == false => {
                            state.errors.push(Box::new(
                                errors::Items {
                                    path: path.to_string(),
                                    detail: "Additional items are not allowed".to_string()
                                }
                            ))
                        },
                        Some(AdditionalKind::Schema(ref url)) => {
                            let schema = scope.resolve(url);
                            if schema.is_some() {
                                let schema = schema.unwrap();
                                for (idx, item) in array[urls.len()..].iter().enumerate() {
                                    let item_path = [path, idx.to_string().as_ref()].join("/");
                                    state.append(schema.validate_in(item, item_path.as_ref()))
                                }
                            } else {
                                state.missing.push(url.clone())
                            }
                        },
                        _ => ()
                    }
                }
            }
            _ => ()
        }

        state
    }
}