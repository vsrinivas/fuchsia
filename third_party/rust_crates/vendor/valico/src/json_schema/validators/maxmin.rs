use serde_json::{Value};

use super::super::errors;
use super::super::scope;

#[allow(missing_copy_implementations)]
pub struct Maximum {
    pub number: f64,
    pub exclusive: bool
}

impl super::Validator for Maximum {
    fn validate(&self, val: &Value, path: &str, _scope: &scope::Scope) -> super::ValidationState {
        let number = nonstrict_process!(val.as_f64(), path);

        let valid = if self.exclusive {
            number < self.number
        } else {
            number <= self.number
        };

        if valid {
            super::ValidationState::new()
        } else {
            val_error!(
                errors::Maximum {
                    path: path.to_string()
                }
            )
        }
    }
}

#[allow(missing_copy_implementations)]
pub struct Minimum {
    pub number: f64,
    pub exclusive: bool
}

impl super::Validator for Minimum {
    fn validate(&self, val: &Value, path: &str, _scope: &scope::Scope) -> super::ValidationState {
        let number = nonstrict_process!(val.as_f64(), path);

        let valid = if self.exclusive {
            number > self.number
        } else {
            number >= self.number
        };

        if valid {
            super::ValidationState::new()
        } else {
            val_error!(
                errors::Minimum {
                    path: path.to_string()
                }
            )
        }
    }
}