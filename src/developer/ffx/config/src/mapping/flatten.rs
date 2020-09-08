use serde_json::Value;

pub(crate) fn flatten<'a, T: Fn(Value) -> Option<Value> + Sync>(
    next: &'a T,
) -> Box<dyn Fn(Value) -> Option<Value> + 'a + Send + Sync> {
    Box::new(move |value| -> Option<Value> {
        if let Value::Array(values) = value {
            values.iter().find_map(|inner_v| next(inner_v.clone()))
        } else {
            next(value)
        }
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::mapping::identity::identity;

    #[test]
    fn test_returns_first() {
        let test = Value::Array(vec![
            Value::String("test1".to_string()),
            Value::String("test2".to_string()),
        ]);
        let result = flatten(&identity)(test);
        assert_eq!(result, Some(Value::String("test1".to_string())));
    }

    #[test]
    fn test_returns_value_if_not_string() {
        let test = Value::Bool(false);
        let result = flatten(&identity)(test);
        assert_eq!(result, Some(Value::Bool(false)));
    }
}
