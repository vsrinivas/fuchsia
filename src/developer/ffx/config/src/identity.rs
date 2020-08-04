use serde_json::Value;

pub(crate) fn identity(value: Value) -> Option<Value> {
    Some(value)
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_returns_first() {
        let test = Value::String("test1".to_string());
        let result = identity(test);
        assert_eq!(result, Some(Value::String("test1".to_string())));
    }
}
