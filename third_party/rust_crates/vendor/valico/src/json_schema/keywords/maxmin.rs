use serde_json::{Value};

use super::super::schema;
use super::super::validators;

macro_rules! kw_minmax{
    ($name:ident, $keyword:expr, $exclusive:expr) => {
        #[allow(missing_copy_implementations)]
        pub struct $name;
        impl super::Keyword for $name {
            fn compile(&self, def: &Value, ctx: &schema::WalkContext) -> super::KeywordResult {
                let maybe_value = def.get($keyword);
                let exclusive = def.get($exclusive);

                if exclusive.is_some() {
                    if !maybe_value.is_some() {
                        return Err(schema::SchemaError::Malformed {
                            path: ctx.fragment.join("/"),
                            detail: "`exclusiveMinimum/exclusiveMaximum` can't go without minimum/maximum".to_string()
                        })
                    }
                }

                if maybe_value.is_some() {
                    let value = maybe_value.unwrap();
                    if value.is_number() {
                        let value = value.as_f64().unwrap();
                        Ok(Some(Box::new(validators::$name {
                            number: value,
                            exclusive: exclusive.is_some() &&
                                       try!(exclusive.unwrap()
                                            .as_bool()
                                            .ok_or_else(||
                                                schema::SchemaError::Malformed {
                                                    path: ctx.fragment.join("/"),
                                                    detail: "`exclusiveMaximum/exclusiveMaximum` must be boolean".to_string()
                                                }
                                            ))
                        })))
                    } else {
                        Err(schema::SchemaError::Malformed {
                            path: ctx.fragment.join("/"),
                            detail: "the `minimum/maximum` value must be a number".to_string()
                        })
                    }
                } else {
                    Ok(None)
                }
            }
        }
    }
}

kw_minmax!(Minimum, "minimum", "exclusiveMinimum");
kw_minmax!(Maximum, "maximum", "exclusiveMaximum");

#[cfg(test)] use super::super::scope;
#[cfg(test)] use jsonway;
#[cfg(test)] use super::super::builder;
#[cfg(test)] use serde_json::to_value;

#[test]
fn validate_maximum() {
    let mut scope = scope::Scope::new();
    let schema = scope.compile_and_return(builder::schema(|s| {
        s.maximum(10f64, false);
    }).into_json(), true).ok().unwrap();

    assert_eq!(schema.validate(&to_value(&9).unwrap()).is_valid(), true);
    assert_eq!(schema.validate(&to_value(&10).unwrap()).is_valid(), true);
    assert_eq!(schema.validate(&to_value(&11).unwrap()).is_valid(), false);
}

#[test]
fn validate_exclusive_maximum() {
    let mut scope = scope::Scope::new();
    let schema = scope.compile_and_return(builder::schema(|s| {
        s.maximum(10f64, true);
    }).into_json(), true).ok().unwrap();

    assert_eq!(schema.validate(&to_value(&9).unwrap()).is_valid(), true);
    assert_eq!(schema.validate(&to_value(&10).unwrap()).is_valid(), false);
    assert_eq!(schema.validate(&to_value(&11).unwrap()).is_valid(), false);
}

#[test]
fn mailformed_maximum() {
    let mut scope = scope::Scope::new();

    assert!(scope.compile_and_return(jsonway::object(|schema| {
        schema.set("maximum", true);
    }).unwrap(), true).is_err());
}

#[test]
fn mailformed_exclusive_maximum() {
    let mut scope = scope::Scope::new();

    assert!(scope.compile_and_return(jsonway::object(|schema| {
        schema.set("exclusiveMaximum", true);
    }).unwrap(), true).is_err());

    assert!(scope.compile_and_return(jsonway::object(|schema| {
        schema.set("maximum", 10);
        schema.set("exclusiveMaximum", "".to_string());
    }).unwrap(), true).is_err());
}

#[test]
fn validate_minumum() {
    let mut scope = scope::Scope::new();
    let schema = scope.compile_and_return(builder::schema(|s| {
        s.minimum(10f64, false);
    }).into_json(), true).ok().unwrap();

    assert_eq!(schema.validate(&to_value(&9).unwrap()).is_valid(), false);
    assert_eq!(schema.validate(&to_value(&10).unwrap()).is_valid(), true);
    assert_eq!(schema.validate(&to_value(&11).unwrap()).is_valid(), true);
}

#[test]
fn validate_exclusive_minimum() {
    let mut scope = scope::Scope::new();
    let schema = scope.compile_and_return(builder::schema(|s| {
        s.minimum(10f64, true);
    }).into_json(), true).ok().unwrap();

    assert_eq!(schema.validate(&to_value(&9).unwrap()).is_valid(), false);
    assert_eq!(schema.validate(&to_value(&10).unwrap()).is_valid(), false);
    assert_eq!(schema.validate(&to_value(&11).unwrap()).is_valid(), true);
}

#[test]
fn mailformed_minumum() {
    let mut scope = scope::Scope::new();

    assert!(scope.compile_and_return(jsonway::object(|schema| {
        schema.set("minimum", true);
    }).unwrap(), true).is_err());
}

#[test]
fn mailformed_exclusive_minumum() {
    let mut scope = scope::Scope::new();

    assert!(scope.compile_and_return(jsonway::object(|schema| {
        schema.set("exclusiveMinimum", true);
    }).unwrap(), true).is_err());

    assert!(scope.compile_and_return(jsonway::object(|schema| {
        schema.set("minimum", 10);
        schema.set("exclusiveMinimum", "".to_string());
    }).unwrap(), true).is_err());
}
