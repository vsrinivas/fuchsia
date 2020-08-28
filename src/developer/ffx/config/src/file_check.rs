use {serde_json::Value, std::path::PathBuf};

pub(crate) fn file_check<'a, T: Fn(Value) -> Option<Value> + Sync>(
    next: &'a T,
) -> Box<dyn Fn(Value) -> Option<Value> + 'a + Send + Sync> {
    Box::new(move |value| -> Option<Value> {
        value.as_str().map(|s| s.to_string()).and_then(|s| {
            if PathBuf::from(s.clone()).exists() {
                next(value)
            } else {
                None
            }
        })
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use super::*;
    use crate::identity::identity;
    use anyhow::{bail, Result};
    use serde_json::json;
    use tempfile::NamedTempFile;

    #[test]
    fn test_file_mapper() -> Result<()> {
        let file = NamedTempFile::new()?;
        if let Some(path) = file.path().to_str() {
            let test = Value::String(path.to_string());
            assert_eq!(file_check(&identity)(test), Some(Value::String(path.to_string())));
            Ok(())
        } else {
            bail!("Unable to get temp file path");
        }
    }

    #[test]
    fn test_file_mapper_returns_none() -> Result<()> {
        let test = json!("/fake_path/should_not_exist");
        assert_eq!(file_check(&identity)(test), None);
        Ok(())
    }
}
