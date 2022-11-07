use serde_json::{Value};
use url;

use super::super::errors;
use super::super::scope;

#[allow(missing_copy_implementations)]
pub struct AllOf {
    pub schemes: Vec<url::Url>,
}

impl super::Validator for AllOf {
    fn validate(&self, val: &Value, path: &str, scope: &scope::Scope) -> super::ValidationState {
        let mut state = super::ValidationState::new();

        for url in self.schemes.iter() {
            let schema = scope.resolve(url);

            if schema.is_some() {
                state.append(schema.unwrap().validate_in(val, path))
            } else {
                state.missing.push(url.clone())
            }
        }

        state
    }
}

#[allow(missing_copy_implementations)]
pub struct AnyOf {
    pub schemes: Vec<url::Url>,
}

impl super::Validator for AnyOf {
    fn validate(&self, val: &Value, path: &str, scope: &scope::Scope) -> super::ValidationState {
        let mut state = super::ValidationState::new();

        let mut states = vec![];
        let mut valid = false;
        for url in self.schemes.iter() {
            let schema = scope.resolve(url);

            if schema.is_some() {
                let current_state = schema.unwrap().validate_in(val, path);

                state.missing.extend(current_state.missing.clone());

                if current_state.is_valid() {
                    valid = true;
                    break;
                } else {
                   states.push(current_state)
                }
            } else {
                state.missing.push(url.clone())
            }
        }

        if !valid {
            state.errors.push(Box::new(
                errors::AnyOf {
                    path: path.to_string(),
                    states: states
                }
            ))
        }


        state
    }
}

#[allow(missing_copy_implementations)]
pub struct OneOf {
    pub schemes: Vec<url::Url>,
}

impl super::Validator for OneOf {
    fn validate(&self, val: &Value, path: &str, scope: &scope::Scope) -> super::ValidationState {
        let mut state = super::ValidationState::new();

        let mut states = vec![];
        let mut valid = 0;
        for url in self.schemes.iter() {
            let schema = scope.resolve(url);

            if schema.is_some() {
                let current_state = schema.unwrap().validate_in(val, path);

                state.missing.extend(current_state.missing.clone());

                if current_state.is_valid() {
                    valid += 1;
                } else {
                   states.push(current_state)
                }
            } else {
                state.missing.push(url.clone())
            }
        }

        if valid != 1 {
            state.errors.push(Box::new(
                errors::OneOf {
                    path: path.to_string(),
                    states: states
                }
            ))
        }


        state
    }
}