use {crate::env_var::environment_variables_mapper, serde_json::Value, std::path::PathBuf};

fn file_check(value: Option<Value>) -> Option<Value> {
    value.map(|v| v.as_str().map(|s| s.to_string())).flatten().and_then(|s| {
        if PathBuf::from(s.clone()).exists() {
            Some(Value::String(s))
        } else {
            None
        }
    })
}

pub(crate) fn file_flatten_env_var(value: Option<Value>) -> Option<Value> {
    value.and_then(|v| {
        if let Value::Array(values) = v {
            values
                .iter()
                .find_map(|inner_v| file_check(environment_variables_mapper(Some(inner_v.clone()))))
        } else {
            file_check(environment_variables_mapper(Some(v)))
        }
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use anyhow::{bail, Result};
    use serde_json::json;
    use tempfile::NamedTempFile;

    #[test]
    fn test_file_mapper() -> Result<()> {
        let file = NamedTempFile::new()?;
        if let Some(path) = file.path().to_str() {
            let test = Some(Value::String(path.to_string()));
            assert_eq!(file_flatten_env_var(test), Some(Value::String(path.to_string())));
            Ok(())
        } else {
            bail!("Unable to get temp file path");
        }
    }

    #[test]
    fn test_file_mapper_returns_first_to_exist() -> Result<()> {
        let file = NamedTempFile::new()?;
        if let Some(path) = file.path().to_str() {
            let test = Some(json!(["/fake_path/should_not_exist", path]));
            assert_eq!(file_flatten_env_var(test), Some(Value::String(path.to_string())));
            Ok(())
        } else {
            bail!("Unable to get temp file path");
        }
    }

    #[test]
    fn test_file_mapper_returns_none() -> Result<()> {
        let test = Some(json!(["/fake_path/should_not_exist"]));
        assert_eq!(file_flatten_env_var(test), None);
        Ok(())
    }
}
