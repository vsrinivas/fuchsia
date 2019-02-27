use serde_json::{Value};

use super::super::schema;
use super::super::validators;
use super::super::helpers;

macro_rules! of_keyword{
    ($name:ident, $kw:expr) => {

        #[allow(missing_copy_implementations)]
        pub struct $name;
        impl super::Keyword for $name {
            fn compile(&self, def: &Value, ctx: &schema::WalkContext) -> super::KeywordResult {
                let of = keyword_key_exists!(def, $kw);

                if of.is_array() {
                    let of = of.as_array().unwrap();

                    if of.len() == 0 {
                        return Err(schema::SchemaError::Malformed {
                            path: ctx.fragment.join("/"),
                            detail: "This array MUST have at least one element.".to_string()
                        })
                    }

                    let mut schemes = vec![];
                    for (idx, scheme) in of.iter().enumerate() {
                        if scheme.is_object() {
                            schemes.push(
                                helpers::alter_fragment_path(ctx.url.clone(), [
                                    ctx.escaped_fragment().as_ref(),
                                    $kw,
                                    idx.to_string().as_ref()
                                ].join("/"))
                            )
                        } else {
                            return Err(schema::SchemaError::Malformed {
                                path: ctx.fragment.join("/"),
                                detail: "Elements of the array MUST be objects.".to_string()
                            })
                        }
                    }

                    Ok(Some(Box::new(validators::$name {
                        schemes: schemes
                    })))
                } else {
                    Err(schema::SchemaError::Malformed {
                        path: ctx.fragment.join("/"),
                        detail: "The value of this keyword MUST be an array.".to_string()
                    })
                }
            }
        }

    }
}

of_keyword!(AllOf, "allOf");
of_keyword!(AnyOf, "anyOf");
of_keyword!(OneOf, "oneOf");

#[cfg(test)] use super::super::scope;
#[cfg(test)] use super::super::builder;
#[cfg(test)] use serde_json::to_value;

#[test]
fn validate_all_of() {
    let mut scope = scope::Scope::new();
    let schema = scope.compile_and_return(builder::schema(|s| {
        s.all_of(|all_of| {
            all_of.push(|schema| {
                schema.minimum(5f64, false);
            });
            all_of.push(|schema| {
                schema.maximum(10f64, false);
            });
        });
    }).into_json(), true).ok().unwrap();

    assert_eq!(schema.validate(&to_value(&7).unwrap()).is_valid(), true);
    assert_eq!(schema.validate(&to_value(&4).unwrap()).is_valid(), false);
    assert_eq!(schema.validate(&to_value(&11).unwrap()).is_valid(), false);
}

#[test]
fn validate_any_of() {
    let mut scope = scope::Scope::new();
    let schema = scope.compile_and_return(builder::schema(|s| {
        s.any_of(|all_of| {
            all_of.push(|schema| {
                schema.maximum(5f64, false);
            });
            all_of.push(|schema| {
                schema.maximum(10f64, false);
            });
        });
    }).into_json(), true).ok().unwrap();

    assert_eq!(schema.validate(&to_value(&5).unwrap()).is_valid(), true);
    assert_eq!(schema.validate(&to_value(&10).unwrap()).is_valid(), true);
    assert_eq!(schema.validate(&to_value(&11).unwrap()).is_valid(), false);
}

#[test]
fn validate_one_of() {
    let mut scope = scope::Scope::new();
    let schema = scope.compile_and_return(builder::schema(|s| {
        s.one_of(|all_of| {
            all_of.push(|schema| {
                schema.maximum(5f64, false);
            });
            all_of.push(|schema| {
                schema.maximum(10f64, false);
            });
        });
    }).into_json(), true).ok().unwrap();

    assert_eq!(schema.validate(&to_value(&5).unwrap()).is_valid(), false);
    assert_eq!(schema.validate(&to_value(&6).unwrap()).is_valid(), true);
    assert_eq!(schema.validate(&to_value(&11).unwrap()).is_valid(), false);
}
