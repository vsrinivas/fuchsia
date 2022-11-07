mod builder;
mod coercers;
mod param;
pub mod errors;
#[macro_use] pub mod validators;

use super::json_schema;

pub use self::param::Param;
pub use self::builder::Builder;
pub use self::coercers::{
    PrimitiveType,
    Coercer,
    StringCoercer,
    I64Coercer,
    U64Coercer,
    F64Coercer,
    BooleanCoercer,
    NullCoercer,
    ArrayCoercer,
    ObjectCoercer,
};

pub fn i64() -> Box<coercers::Coercer + Send + Sync> { Box::new(coercers::I64Coercer) }
pub fn u64() -> Box<coercers::Coercer + Send + Sync> { Box::new(coercers::U64Coercer) }
pub fn f64() -> Box<coercers::Coercer + Send + Sync> { Box::new(coercers::F64Coercer) }
pub fn string() -> Box<coercers::Coercer + Send + Sync> { Box::new(coercers::StringCoercer) }
pub fn boolean() -> Box<coercers::Coercer + Send + Sync> { Box::new(coercers::BooleanCoercer) }
pub fn null() -> Box<coercers::Coercer + Send + Sync> { Box::new(coercers::NullCoercer) }
pub fn array() -> Box<coercers::Coercer + Send + Sync> { Box::new(coercers::ArrayCoercer::new()) }
pub fn array_of(coercer: Box<coercers::Coercer + Send + Sync>) -> Box<coercers::Coercer + Send + Sync> {
    Box::new(coercers::ArrayCoercer::of_type(coercer))
}

pub fn encoded_array(separator: &str) -> Box<coercers::Coercer + Send + Sync> {
    Box::new(coercers::ArrayCoercer::encoded(separator.to_string()))
}

pub fn encoded_array_of(separator: &str, coercer: Box<coercers::Coercer + Send + Sync>) -> Box<coercers::Coercer + Send + Sync> {
    Box::new(coercers::ArrayCoercer::encoded_of(separator.to_string(), coercer))
}

pub fn object() -> Box<coercers::Coercer + Send + Sync> { Box::new(coercers::ObjectCoercer) }

pub struct ExtendedResult<T> {
    value: T,
    state: json_schema::ValidationState
}

impl<T> ExtendedResult<T> {
    pub fn new(value: T) -> ExtendedResult<T> {
        ExtendedResult {
            value: value,
            state: json_schema::ValidationState::new()
        }
    }

    pub fn with_errors(value: T, errors: super::ValicoErrors) -> ExtendedResult<T> {
        ExtendedResult {
            value: value,
            state: json_schema::ValidationState {
                errors: errors,
                missing: vec![]
            }
        }
    }

    pub fn is_valid(&self) -> bool {
        self.state.is_valid()
    }

    pub fn append(&mut self, second: json_schema::ValidationState) {
        self.state.append(second);
    }
}