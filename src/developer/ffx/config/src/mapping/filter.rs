use serde_json::Value;

pub(crate) fn filter<'a, T: Fn(Value) -> Option<Value> + Sync>(
    next: &'a T,
) -> Box<dyn Fn(Value) -> Option<Value> + 'a + Send + Sync> {
    Box::new(move |value| -> Option<Value> {
        if let Value::Array(values) = value {
            let result: Vec<Value> =
                values.iter().filter_map(|inner_v| next(inner_v.clone())).collect();
            if result.len() == 0 {
                None
            } else {
                Some(Value::Array(result))
            }
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
    use serde_json::json;

    #[test]
    fn test_returns_all() {
        let test = json!(["test1", "test2"]);
        let result = filter(&identity)(test);
        assert_eq!(result, Some(json!(["test1", "test2"])));
    }

    #[test]
    fn test_returns_value_if_not_array() {
        let test = Value::Bool(false);
        let result = filter(&identity)(test);
        assert_eq!(result, Some(Value::Bool(false)));
    }

    #[test]
    fn test_returns_none() {
        let test = Value::Bool(false);
        let result = filter(&|_| -> Option<Value> { None })(test);
        assert_eq!(result, None);
    }
}
