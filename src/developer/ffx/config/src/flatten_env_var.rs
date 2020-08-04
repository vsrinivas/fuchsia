use {crate::env_var::environment_variables_mapper, serde_json::Value};

pub(crate) fn flatten_env_var(value: Value) -> Option<Value> {
    if let Value::Array(values) = value {
        values.iter().find_map(|inner_v| environment_variables_mapper(inner_v.clone()))
    } else {
        environment_variables_mapper(value)
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_returns_first() {
        let test = Value::Array(vec![
            Value::String("test1".to_string()),
            Value::String("test2".to_string()),
        ]);
        let result = flatten_env_var(test);
        assert_eq!(result, Some(Value::String("test1".to_string())));
    }

    #[test]
    fn test_returns_value_if_not_string() {
        let test = Value::Bool(false);
        let result = flatten_env_var(test);
        assert_eq!(result, Some(Value::Bool(false)));
    }
}
