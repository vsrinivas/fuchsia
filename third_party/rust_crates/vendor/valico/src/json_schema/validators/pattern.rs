use regex;
use serde_json::{Value};

use super::super::errors;
use super::super::scope;

#[allow(missing_copy_implementations)]
pub struct Pattern {
    pub regex: regex::Regex
}

impl super::Validator for Pattern {
    fn validate(&self, val: &Value, path: &str, _scope: &scope::Scope) -> super::ValidationState {
        let string = nonstrict_process!(val.as_str(), path);

        if self.regex.is_match(string) {
            super::ValidationState::new()
        } else {
            val_error!(
                errors::Pattern {
                    path: path.to_string()
                }
            )
        }
    }
}
