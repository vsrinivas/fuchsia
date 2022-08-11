// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::util::digest_path,
    crate::writer::{OutputSink, Writer},
    anyhow::{bail, Context as _, Result},
    fidl_fuchsia_fuzzer::Input as FidlInput,
    futures::{AsyncReadExt, AsyncWriteExt},
    std::fs,
    std::path::{Path, PathBuf},
};

/// Represents a sequence of bytes, paired with a `fuchsia.fuzzer.Input`.
///
/// The `fuchsia.fuzzer.Input` FIDL struct is used to transport test inputs and artifacts between
/// a target device running a fuzzer and a development host running the `ffx fuzz` plugin. This
/// struct and that FIDL struct are created in pairs. The FIDL struct can be sent to the target
/// device, and this struct can be used to transmit the actual test input data to the target
/// device.
#[derive(Debug)]
pub struct Input {
    socket: fidl::Socket,
    data: Vec<u8>,
}

impl Input {
    /// Generates an `Input` from a  string.
    ///
    /// The `input` string can be either a file name or a hex-encoded value. This method also
    /// returns a `fuchsia.fuzzer.Input` that can be sent via FIDL calls to receive this object's
    /// data. The `writer` is used to alert the user if there is ambiguity about how to interpret
    /// `input`.
    ///
    /// Returns an error if the `input` string is neither valid hex nor a valid path to a file.
    ///
    pub fn from_str<S, O>(input: S, writer: &Writer<O>) -> Result<(FidlInput, Input)>
    where
        S: AsRef<str>,
        O: OutputSink,
    {
        let input = input.as_ref();
        let hex_result = hex::decode(input);
        let fs_result = fs::read(input);
        let input_data = match (hex_result, fs_result) {
            (Ok(input_data), Err(_)) => input_data,
            (Err(_), Ok(input_data)) => input_data,
            (Ok(input_data), Ok(_)) => {
                writer.print("WARNING: ");
                writer.print(input);
                writer.println("can be interpreted as either a hex string or a file.");
                writer.println("The input will be treated as a hex string.");
                writer.println("To force treatment as a file, include more of the path, e.g.");
                writer.print("  ./");
                writer.println(input);
                input_data
            }
            (Err(_), Err(e)) => bail!("failed to read fuzzer input: {}", e),
        };
        Input::create(input_data)
    }

    /// Generates an `Input` from a filesystem path.
    ///
    /// This method also returns a `fuchsia.fuzzer.Input` that can be sent via FIDL calls to receive
    /// this object's data.
    ///
    /// Returns an error if the `path` is invalid.
    ///
    pub fn from_path<P: AsRef<Path>>(path: P) -> Result<(FidlInput, Input)> {
        let path = path.as_ref();
        let input_data = fs::read(path)
            .with_context(|| format!("failed to read '{}'", path.to_string_lossy()))?;
        Input::create(input_data)
    }

    /// Creates an `Input` from a sequence of bytes.
    ///
    /// This method also returns a `fuchsia.fuzzer.Input` that can be sent via FIDL calls to receive
    /// this object's data.
    pub fn create(input_data: Vec<u8>) -> Result<(FidlInput, Self)> {
        let (reader, writer) = fidl::Socket::create(fidl::SocketOpts::STREAM)
            .context("failed to create socket for fuzz input")?;
        let fidl_input = FidlInput { socket: reader, size: input_data.len() as u64 };
        Ok((fidl_input, Self { socket: writer, data: input_data }))
    }

    /// Returns the length of this object's data.
    pub fn len(&self) -> usize {
        self.data.len()
    }

    /// Writes the object's data to its internal socket.
    ///
    /// This will deliver the data to the `fuchsia.fuzzer.Input` created with this object.
    pub async fn send(self) -> Result<()> {
        let mut writer = fidl::AsyncSocket::from_socket(self.socket)
            .context("failed to convert socket for sending fuzz input")?;
        writer.write(&self.data).await.context("failed to write fuzz input")?;
        Ok(())
    }
}

/// Reads data from a `fuchsia.fuzzer.Input` and saves it locally.
///
/// Reads data from the `input` and saves it to a file within `out_dir` and with the given `prefix`,
/// if provided. On success, returns the path to the file.
///
/// Returns an error if it fails to read the data from the `input` or if it fails to write the data
/// to the file.
///
/// See also `utils::digest_path`.
///
pub async fn save_input<P>(input: FidlInput, out_dir: P, prefix: Option<&str>) -> Result<PathBuf>
where
    P: AsRef<Path>,
{
    let reader = fidl::AsyncSocket::from_socket(input.socket)
        .context("failed to convert socket for saving fuzz input")?;
    let mut data = Vec::new();
    reader
        .take(input.size)
        .read_to_end(&mut data)
        .await
        .context("failed to read fuzz input from socket")?;
    let path = digest_path(out_dir, prefix, &data);
    fs::write(&path, data)
        .with_context(|| format!("failed to write fuzz input to '{}'", path.to_string_lossy()))?;
    Ok(path)
}

#[cfg(test)]
pub mod test_fixtures {
    use {
        anyhow::{Context as _, Result},
        std::fs,
        std::path::Path,
    };

    /// Verifies that the input was actually written and matches its expected contents.
    pub fn verify_saved<P: AsRef<Path>>(saved: P, data: &[u8]) -> Result<()> {
        let saved = saved.as_ref();
        let actual = fs::read(saved)
            .with_context(|| format!("failed to read '{}'", saved.to_string_lossy()))?;
        assert_eq!(actual, data);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::test_fixtures::verify_saved,
        super::{save_input, Input},
        crate::util::digest_path,
        crate::util::test_fixtures::Test,
        anyhow::Result,
        fidl_fuchsia_fuzzer::Input as FidlInput,
        futures::{join, AsyncReadExt},
        std::fs::File,
        std::io::Write,
    };

    #[fuchsia::test]
    fn test_from_str() -> Result<()> {
        let test = Test::try_new()?;
        let writer = test.writer();
        let input_dir = test.create_dir("inputs")?;

        // Missing file.
        let input1 = input_dir.join("input1");
        let actual = format!("{:?}", Input::from_str(input1.to_string_lossy(), writer));
        assert!(actual.contains("failed to read fuzzer input"));

        // Empty file.
        let mut file = File::create(&input1)?;
        let (fidl_input, input) = Input::from_str(input1.to_string_lossy(), writer)?;
        assert_eq!(fidl_input.size, 0);
        assert!(input.data.is_empty());

        // File with data.
        file.write_all(b"data")?;
        let (fidl_input, input) = Input::from_str(input1.to_string_lossy(), writer)?;
        assert_eq!(fidl_input.size, 4);
        assert_eq!(input.data, b"data");

        // Hex value.
        let (fidl_input, input) = Input::from_str("64617461", writer)?;
        assert_eq!(fidl_input.size, 4);
        assert_eq!(input.data, b"data");
        Ok(())
    }

    #[fuchsia::test]
    fn test_from_path() -> Result<()> {
        let test = Test::try_new()?;
        let mut path = test.create_dir("inputs")?;
        path.push("input");
        assert!(Input::from_path(&path).is_err());

        let mut file = File::create(&path)?;
        let (fidl_input, input) = Input::from_path(&path)?;
        assert_eq!(fidl_input.size, 0);
        assert!(input.data.is_empty());

        file.write_all(b"data")?;
        let (fidl_input, input) = Input::from_path(&path)?;
        assert_eq!(fidl_input.size, 4);
        assert_eq!(input.data, b"data");
        Ok(())
    }

    #[fuchsia::test]
    async fn test_send() -> Result<()> {
        async fn recv(fidl_input: FidlInput, expected: &[u8]) {
            let mut reader =
                fidl::AsyncSocket::from_socket(fidl_input.socket).expect("from_socket failed");
            let mut buf = Vec::new();
            let num_read = reader.read_to_end(&mut buf).await.expect("read_to_end failed");
            assert_eq!(num_read as u64, fidl_input.size);
            assert_eq!(buf, expected);
        }
        let (fidl_input, input) = Input::create(b"".to_vec())?;
        assert!(join!(input.send(), recv(fidl_input, b"")).0.is_ok());
        let (fidl_input, input) = Input::create(b"data".to_vec())?;
        assert!(join!(input.send(), recv(fidl_input, b"data")).0.is_ok());
        Ok(())
    }

    #[fuchsia::test]
    async fn test_save() -> Result<()> {
        let test = Test::try_new()?;
        let saved_dir = test.create_dir("saved")?;

        let (reader, writer) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
        let fidl_input = FidlInput { socket: reader, size: 0 };
        let input = Input { socket: writer, data: Vec::new() };
        let send_fut = input.send();
        let save_fut = save_input(fidl_input, &saved_dir, None);
        let results = join!(send_fut, save_fut);
        assert!(results.0.is_ok());
        assert!(results.1.is_ok());
        let saved = digest_path(&saved_dir, None, b"");
        verify_saved(&saved, b"")?;

        let (reader, writer) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
        let fidl_input = FidlInput { socket: reader, size: 4 };
        let input = Input { socket: writer, data: b"data".to_vec() };
        let send_fut = input.send();
        let save_fut = save_input(fidl_input, &saved_dir, Some("test"));
        let results = join!(send_fut, save_fut);
        assert!(results.0.is_ok());
        assert!(results.1.is_ok());
        let saved = digest_path(&saved_dir, Some("test"), b"data");
        verify_saved(&saved, b"data")?;

        Ok(())
    }
}
