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

/// Represents an `Input` that can send or read data from an associated `FidlInput`.
#[derive(Debug)]
pub struct InputPair {
    /// Socket and size used to send input data to or receive input data from a fuzzer.
    pub fidl_input: FidlInput,

    /// Client-side representation of a fuzzer input.
    pub input: Input,
}

impl From<(FidlInput, Input)> for InputPair {
    fn from(tuple: (FidlInput, Input)) -> Self {
        InputPair { fidl_input: tuple.0, input: tuple.1 }
    }
}

impl InputPair {
    /// Generates an input pair from a  string.
    ///
    /// The `input` string can be either a file name or a hex-encoded value. This method also
    /// returns a `fuchsia.fuzzer.Input` that can be sent via FIDL calls to receive this object's
    /// data. The `writer` is used to alert the user if there is ambiguity about how to interpret
    /// `input`.
    ///
    /// Returns an error if the `input` string is neither valid hex nor a valid path to a file.
    ///
    pub fn try_from_str<S, O>(input: S, writer: &Writer<O>) -> Result<Self>
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
        InputPair::try_from_data(input_data)
    }

    /// Generates an input pair from a filesystem path.
    ///
    /// Returns an error if the `path` is invalid.
    ///
    pub fn try_from_path<P: AsRef<Path>>(path: P) -> Result<Self> {
        let path = path.as_ref();
        let input_data = fs::read(path)
            .with_context(|| format!("failed to read '{}'", path.to_string_lossy()))?;
        InputPair::try_from_data(input_data)
    }

    /// Creates an input pair from a sequence of bytes.
    pub fn try_from_data(input_data: Vec<u8>) -> Result<Self> {
        let (reader, writer) = fidl::Socket::create(fidl::SocketOpts::STREAM)
            .context("failed to create socket for fuzz input")?;
        let fidl_input = FidlInput { socket: reader, size: input_data.len() as u64 };
        let input = Input { socket: Some(writer), data: input_data };
        Ok(InputPair::from((fidl_input, input)))
    }

    /// Destructures the object into a `FidlInput` and an `Input`.
    pub fn as_tuple(self) -> (FidlInput, Input) {
        (self.fidl_input, self.input)
    }

    /// Returns the length of this object's data.
    pub fn len(&self) -> usize {
        self.input.data.len()
    }
}

/// Represents a sequence of bytes, paired with a `fuchsia.fuzzer.Input`.
///
/// The `fuchsia.fuzzer.Input` FIDL struct is used to transport test inputs and artifacts between
/// a target device running a fuzzer and a development host running the `ffx fuzz` plugin. This
/// struct and that FIDL struct are created in pairs. The FIDL struct can be sent to the target
/// device, and this struct can be used to transmit the actual test input data to the target
/// device.
#[derive(Debug)]
pub struct Input {
    socket: Option<fidl::Socket>,

    /// The received data
    pub data: Vec<u8>,
}

impl Input {
    /// Writes the object's data to its internal socket.
    ///
    /// This will deliver the data to the `fuchsia.fuzzer.Input` created with this object.
    pub async fn send(mut self) -> Result<()> {
        let socket = self.socket.take().context("input already sent")?;
        let mut writer = fidl::AsyncSocket::from_socket(socket)
            .context("failed to convert socket for sending fuzz input")?;
        writer.write_all(&self.data).await.context("failed to write fuzz input")?;
        Ok(())
    }

    /// Reads the object's data from a `FidlInput`.
    ///
    /// Returns an error if unable to read from the underlying socket.
    pub async fn try_receive(fidl_input: FidlInput) -> Result<Self> {
        let mut data = Vec::new();
        let reader = fidl::AsyncSocket::from_socket(fidl_input.socket)
            .context("failed to convert socket for saving fuzz input")?;
        reader
            .take(fidl_input.size)
            .read_to_end(&mut data)
            .await
            .context("failed to read fuzz input from socket")?;
        Ok(Input { socket: None, data })
    }
}

/// Reads fuzzer input data from a `FidlInput` and saves it locally.
///
/// Returns the path to the file on success. Returns an error if it fails to read the data from the
/// `input` or if it fails to write the data to the file.
///
/// See also `utils::digest_path`.
///
pub async fn save_input<P: AsRef<Path>>(fidl_input: FidlInput, out_dir: P) -> Result<PathBuf> {
    let input =
        Input::try_receive(fidl_input).await.context("failed to receive fuzzer input data")?;
    let path = digest_path(out_dir, None, &input.data);
    fs::write(&path, input.data)
        .with_context(|| format!("failed to write fuzzer input to '{}'", path.to_string_lossy()))?;
    Ok(path)
}

#[cfg(test)]
mod tests {
    use {
        super::{save_input, Input},
        crate::util::digest_path,
        anyhow::Result,
        fidl_fuchsia_fuzzer::Input as FidlInput,
        fuchsia_fuzzctl::InputPair,
        fuchsia_fuzzctl_test::{verify_saved, Test},
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
        let actual = format!("{:?}", InputPair::try_from_str(input1.to_string_lossy(), writer));
        assert!(actual.contains("failed to read fuzzer input"));

        // Empty file.
        let mut file = File::create(&input1)?;
        let input_pair = InputPair::try_from_str(input1.to_string_lossy(), writer)?;
        let (fidl_input, input) = input_pair.as_tuple();
        assert_eq!(fidl_input.size, 0);
        assert!(input.data.is_empty());

        // File with data.
        file.write_all(b"data")?;
        let input_pair = InputPair::try_from_str(input1.to_string_lossy(), writer)?;
        let (fidl_input, input) = input_pair.as_tuple();
        assert_eq!(fidl_input.size, 4);
        assert_eq!(input.data, b"data");

        // Hex value.
        let input_pair = InputPair::try_from_str("64617461", writer)?;
        let (fidl_input, input) = input_pair.as_tuple();
        assert_eq!(fidl_input.size, 4);
        assert_eq!(input.data, b"data");
        Ok(())
    }

    #[fuchsia::test]
    fn test_from_path() -> Result<()> {
        let test = Test::try_new()?;
        let mut path = test.create_dir("inputs")?;
        path.push("input");
        assert!(InputPair::try_from_path(&path).is_err());

        let mut file = File::create(&path)?;
        let input_pair = InputPair::try_from_path(&path)?;
        let (fidl_input, input) = input_pair.as_tuple();
        assert_eq!(fidl_input.size, 0);
        assert!(input.data.is_empty());

        file.write_all(b"data")?;
        let input_pair = InputPair::try_from_path(&path)?;
        let (fidl_input, input) = input_pair.as_tuple();
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
        let input_pair = InputPair::try_from_data(b"".to_vec())?;
        let (fidl_input, input) = input_pair.as_tuple();
        assert!(join!(input.send(), recv(fidl_input, b"")).0.is_ok());
        let input_pair = InputPair::try_from_data(b"data".to_vec())?;
        let (fidl_input, input) = input_pair.as_tuple();
        assert!(join!(input.send(), recv(fidl_input, b"data")).0.is_ok());
        Ok(())
    }

    #[fuchsia::test]
    async fn test_save_input() -> Result<()> {
        let test = Test::try_new()?;
        let saved_dir = test.create_dir("saved")?;

        let (reader, writer) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
        let fidl_input = FidlInput { socket: reader, size: 0 };
        let input = Input { socket: Some(writer), data: Vec::new() };
        let send_fut = input.send();
        let save_fut = save_input(fidl_input, &saved_dir);
        let results = join!(send_fut, save_fut);
        assert!(results.0.is_ok());
        assert!(results.1.is_ok());
        let saved = digest_path(&saved_dir, None, b"");
        verify_saved(&saved, b"")?;
        Ok(())
    }
}
