use serde_json::{Value};
use std::collections;

use super::super::schema;
use super::super::validators;
use super::super::helpers;

#[allow(missing_copy_implementations)]
pub struct Dependencies;
impl super::Keyword for Dependencies {
    fn compile(&self, def: &Value, ctx: &schema::WalkContext) -> super::KeywordResult {
        let deps = keyword_key_exists!(def, "dependencies");

        if !deps.is_object() {
            return Err(schema::SchemaError::Malformed {
                path: ctx.fragment.join("/"),
                detail: "The value of this keyword MUST be an object.".to_string()
            })
        }

        let deps = deps.as_object().unwrap();
        let mut items = collections::HashMap::new();

        for (key, item) in deps.iter() {
            if item.is_object() {

                items.insert(key.clone(), validators::dependencies::DepKind::Schema(
                    helpers::alter_fragment_path(ctx.url.clone(), [
                        ctx.escaped_fragment().as_ref(),
                        "dependencies",
                        helpers::encode(key).as_ref()
                    ].join("/"))
                ));

            } else if item.is_array() {

                let item = item.as_array().unwrap();

                if item.len() == 0 {
                    return Err(schema::SchemaError::Malformed {
                        path: ctx.fragment.join("/"),
                        detail: "If the value is an array, it MUST have at least one element.".to_string()
                    })
                }

                let mut keys = vec![];

                for key in item.iter() {
                    if key.is_string() {
                        keys.push(key.as_str().unwrap().to_string())
                    } else {
                        return Err(schema::SchemaError::Malformed {
                            path: ctx.fragment.join("/"),
                            detail: "Each element MUST be a string, and elements in the array MUST be unique.".to_string()
                        })
                    }
                }

                items.insert(key.clone(), validators::dependencies::DepKind::Property(
                    keys
                ));

            } else {
                return Err(schema::SchemaError::Malformed {
                    path: ctx.fragment.join("/"),
                    detail: "Each value of this object MUST be either an object or an array.".to_string()
                })
            }
        }

        Ok(Some(Box::new(validators::Dependencies {
            items: items
        })))

    }
}

#[cfg(test)] use super::super::scope;
#[cfg(test)] use super::super::builder;
#[cfg(test)] use jsonway;

#[test]
fn validate_dependencies() {
    let mut scope = scope::Scope::new();
    let schema = scope.compile_and_return(builder::schema(|s| {
        s.dependencies(|deps| {
            deps.schema("isbn", |isbn| {
                isbn.required(vec!["price".to_string()]);
                isbn.properties(|props| {
                    props.insert("price", |price| {
                        price.multiple_of(5f64);
                    })
                })
            });
            deps.property("item_id", vec!["item_name".to_string()]);
        });
    }).into_json(), true).ok().unwrap();

    assert_eq!(schema.validate(&jsonway::object(|obj| {
        obj.set("isbn", "some_isbn".to_string());
    }).unwrap()).is_valid(), false);

    assert_eq!(schema.validate(&jsonway::object(|obj| {
        obj.set("isbn", "some_isbn".to_string());
        obj.set("price", 773);
    }).unwrap()).is_valid(), false);

    assert_eq!(schema.validate(&jsonway::object(|obj| {
        obj.set("isbn", "some_isbn".to_string());
        obj.set("price", 775);
    }).unwrap()).is_valid(), true);

    assert_eq!(schema.validate(&jsonway::object(|obj| {
        obj.set("item_id", "some_id".to_string());
    }).unwrap()).is_valid(), false);

    assert_eq!(schema.validate(&jsonway::object(|obj| {
        obj.set("item_id", "some_id".to_string());
        obj.set("item_name", "some_name".to_string());
    }).unwrap()).is_valid(), true);

}

#[test]
fn malformed() {
    let mut scope = scope::Scope::new();

    assert!(scope.compile_and_return(jsonway::object(|schema| {
        schema.object("dependencies", |deps| {
            deps.set("isbn", 10);
        });
    }).unwrap(), true).is_err());

    assert!(scope.compile_and_return(jsonway::object(|schema| {
        schema.object("dependencies", |deps| {
            deps.array("item_id", |item_id| {
                item_id.push(10)
            });
        });
    }).unwrap(), true).is_err());
}
