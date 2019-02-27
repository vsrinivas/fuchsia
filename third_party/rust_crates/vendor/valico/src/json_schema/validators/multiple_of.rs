use serde_json::{Value};

use super::super::errors;
use super::super::scope;
use std::f64;

#[allow(missing_copy_implementations)]
pub struct MultipleOf {
    pub number: f64
}

impl super::Validator for MultipleOf {
    fn validate(&self, val: &Value, path: &str, _scope: &scope::Scope) -> super::ValidationState {
        let number = nonstrict_process!(val.as_f64(), path);

        let valid = if (number.fract() == 0f64) && (self.number.fract() == 0f64) {
            (number % self.number) == 0f64
        } else {
            let remainder: f64 = (number/self.number) % 1f64;
            !(remainder >= f64::EPSILON) && (remainder < (1f64 - f64::EPSILON))
        };

        if valid {
            super::ValidationState::new()
        } else {
            val_error!(
                errors::MultipleOf {
                    path: path.to_string()
                }
            )
        }
    }
}