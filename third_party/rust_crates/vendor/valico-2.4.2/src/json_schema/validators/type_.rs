use serde_json::{Value};

use super::super::errors;
use super::super::scope;

use json_schema;

#[derive(Debug)]
pub enum TypeKind {
    Single(json_schema::PrimitiveType),
    Set(Vec<json_schema::PrimitiveType>)
}

#[allow(missing_copy_implementations)]
pub struct Type {
    pub item: TypeKind
}

fn check_type(val: &Value, ty: &json_schema::PrimitiveType) -> bool {
    match ty {
        &json_schema::PrimitiveType::Array => val.is_array(),
        &json_schema::PrimitiveType::Boolean => val.is_boolean(),
        &json_schema::PrimitiveType::Integer => val.is_u64() || val.is_i64(),
        &json_schema::PrimitiveType::Number => val.is_number(),
        &json_schema::PrimitiveType::Null => val.is_null(),
        &json_schema::PrimitiveType::Object => val.is_object(),
        &json_schema::PrimitiveType::String => val.is_string(),
    }
}

impl super::Validator for Type {
    fn validate(&self, val: &Value, path: &str, _scope: &scope::Scope) -> super::ValidationState {
        let mut state = super::ValidationState::new();

        match self.item {
            TypeKind::Single(ref t) => {
                if !check_type(val, t) {
                    state.errors.push(Box::new(
                        errors::WrongType {
                            path: path.to_string(),
                            detail: format!("The value must be {}", t)
                        }
                    ))
                }
            },
            TypeKind::Set(ref set) => {
                let mut is_type_match = false;
                for ty in set.iter() {
                    if check_type(val, ty) {
                        is_type_match = true;
                        break;
                    }
                }

                if !is_type_match {
                    state.errors.push(Box::new(
                        errors::WrongType {
                            path: path.to_string(),
                            detail: format!("The value must be any of: {}", set.iter().map(|ty| ty.to_string()).collect::<Vec<String>>().join(", "))
                        }
                    ))
                }
            }
        }

        state
    }
}