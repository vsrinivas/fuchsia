// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The ffx main function expects a anyhow::Result from ffx plugins. If the Result is an Err it be
/// downcast to FfxError, and if successful this error is presented as a user-readable error. All
/// other error types are printed with full context and a BUG prefix, guiding the user to file bugs
/// to improve the error condition that they have experienced, with a goal to maximize actionable
/// errors over time.
// TODO(fxbug.dev/57592): consider extending this to allow custom types from plugins.
#[derive(thiserror::Error, Debug)]
pub enum FfxError {
    #[error("{}", .0)]
    Error(#[source] anyhow::Error, i32 /* Error status code */),
}

// Utility macro for constructing a FfxError::Error with a simple error string.
#[macro_export]
macro_rules! ffx_error {
    ($error_message: expr) => {{
        $crate::FfxError::Error(anyhow::anyhow!($error_message), 1)
    }};
    ($fmt:expr, $($arg:tt)*) => {
        $crate::ffx_error!(format!($fmt, $($arg)*));
    };
}

#[macro_export]
macro_rules! ffx_error_with_code {
    ($error_code:expr, $error_message:expr $(,)?) => {{
        $crate::FfxError::Error(anyhow::anyhow!($error_message), $error_code)
    }};
    ($error_code:expr, $fmt:expr, $($arg:tt)*) => {
        $crate::ffx_error_with_code!($error_code, format!($fmt, $($arg)*));
    };
}

#[macro_export]
macro_rules! ffx_bail {
    ($msg:literal $(,)?) => {
        anyhow::bail!($crate::ffx_error!($msg))
    };
    ($fmt:expr, $($arg:tt)*) => {
        anyhow::bail!($crate::ffx_error!($fmt, $($arg)*));
    };
}

#[macro_export]
macro_rules! ffx_bail_with_code {
    ($code:literal, $msg:literal $(,)?) => {
        anyhow::bail!($crate::ffx_error_with_code!($code, $msg))
    };
    ($code:expr, $fmt:expr, $($arg:tt)*) => {
        anyhow::bail!($crate::ffx_error_with_code!($code, $fmt, $($arg)*));
    };
}

pub trait ResultExt {
    fn ffx_error<'a>(&'a self) -> Option<&'a FfxError>;

    fn exit_code(&self) -> i32;
}

impl<T> ResultExt for anyhow::Result<T> {
    fn ffx_error<'a>(&'a self) -> Option<&'a FfxError> {
        match self {
            Ok(_) => None,
            Err(ref err) => err.ffx_error(),
        }
    }

    fn exit_code(&self) -> i32 {
        match self {
            Ok(_) => 0,
            Err(ref err) => err.exit_code(),
        }
    }
}

impl ResultExt for anyhow::Error {
    fn ffx_error<'a>(&'a self) -> Option<&'a FfxError> {
        self.downcast_ref::<FfxError>()
    }

    fn exit_code(&self) -> i32 {
        match self.ffx_error() {
            Some(FfxError::Error(_, code)) => *code,
            _ => 1,
        }
    }
}

const BUG_LINE: &str = "BUG: An internal command error occurred.";
pub fn write_result<T>(
    result: &anyhow::Result<T>,
    w: &mut dyn std::io::Write,
) -> std::io::Result<()> {
    if let Some(err) = result.ffx_error() {
        writeln!(w, "{}", err)
    } else if let Err(err) = result {
        writeln!(w, "{}\n{:?}", BUG_LINE, err)
    } else {
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::*;
    use matches::assert_matches;
    use std::io::Cursor;

    const FFX_STR: &str = "I am an ffx error";
    const ERR_STR: &str = "I am not an ffx error";

    #[test]
    fn test_ffx_result_extension() {
        let err = Result::<()>::Err(anyhow!(ERR_STR));
        assert!(err.ffx_error().is_none());

        let err = Result::<()>::Err(Error::new(ffx_error!(FFX_STR)));
        assert_matches!(err.ffx_error(), Some(FfxError::Error(_, _)));
    }

    #[test]
    fn test_ffx_err_extension() {
        let err = anyhow!(ERR_STR);
        assert!(err.ffx_error().is_none());

        let err = Error::new(ffx_error!(FFX_STR));
        assert_matches!(err.ffx_error(), Some(FfxError::Error(_, _)));
    }

    #[test]
    fn test_write_result_ffx_error() {
        let err = Result::<()>::Err(Error::new(ffx_error!(FFX_STR)));
        let mut cursor = Cursor::new(Vec::new());

        assert_matches!(write_result(&err, &mut cursor), Ok(_));

        assert!(String::from_utf8(cursor.into_inner()).unwrap().contains(FFX_STR));
    }

    #[test]
    fn test_write_result_arbitrary_error() {
        let err = Result::<()>::Err(anyhow!(ERR_STR));
        let mut cursor = Cursor::new(Vec::new());

        assert_matches!(write_result(&err, &mut cursor), Ok(_));

        let err_str = String::from_utf8(cursor.into_inner()).unwrap();
        assert!(err_str.contains(BUG_LINE));
        assert!(err_str.contains(ERR_STR));
    }

    #[test]
    fn test_result_ext_exit_code_ffx_error() {
        let err = Result::<()>::Err(Error::new(ffx_error_with_code!(42, FFX_STR)));
        assert_eq!(err.exit_code(), 42);
    }

    #[test]
    fn test_result_ext_exit_code_arbitrary_error() {
        let err = Result::<()>::Err(anyhow!(ERR_STR));
        assert_eq!(err.exit_code(), 1);
    }

    #[test]
    fn test_err_ext_exit_code_ffx_error() {
        let err = Error::new(ffx_error_with_code!(42, FFX_STR));
        assert_eq!(err.exit_code(), 42);
    }

    #[test]
    fn test_err_ext_exit_code_arbitrary_error() {
        let err = anyhow!(ERR_STR);
        assert_eq!(err.exit_code(), 1);
    }
}
