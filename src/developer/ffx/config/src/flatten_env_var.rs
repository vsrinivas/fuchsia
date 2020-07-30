use {crate::env_var::environment_variables_mapper, serde_json::Value};

pub(crate) fn flatten_env_var(value: Option<Value>) -> Option<Value> {
    value.and_then(|v| {
        if let Value::Array(values) = v {
            values.iter().find_map(|inner_v| environment_variables_mapper(Some(inner_v.clone())))
        } else {
            environment_variables_mapper(Some(v))
        }
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_returns_first() {
        let test = Some(Value::Array(vec![
            Value::String("test1".to_string()),
            Value::String("test2".to_string()),
        ]));
        let result = flatten_env_var(test);
        assert_eq!(result, Some(Value::String("test1".to_string())));
    }
}
