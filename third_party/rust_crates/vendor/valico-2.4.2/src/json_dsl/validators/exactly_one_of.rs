use serde_json::{Value};

use super::super::errors;

pub struct ExactlyOneOf {
    params: Vec<String>
}

impl ExactlyOneOf {
    pub fn new(params: &[&str]) -> ExactlyOneOf {
        ExactlyOneOf {
            params: params.iter().map(|s| s.to_string()).collect()
        }
    }
}

impl super::Validator for ExactlyOneOf {
    fn validate(&self, val: &Value, path: &str) -> super::ValidatorResult {

        let object = strict_process!(val.as_object(), path, "The value must be an object");

        let mut matched = vec![];
        for param in self.params.iter() {
            if object.contains_key(param) { matched.push(param.clone()); }
        }

        let len = matched.len();
        if len == 1 {
            Ok(())
        } else if len > 1 {
            Err(vec![
                Box::new(errors::ExactlyOne {
                    path: path.to_string(),
                    detail: Some("Exactly one is allowed at one time".to_string()),
                    params: matched
                })
            ])
        } else {
            Err(vec![
                Box::new(errors::ExactlyOne {
                    path: path.to_string(),
                    detail: Some("Exactly one must be present".to_string()),
                    params: self.params.clone()
                })
            ])
        }
    }
}