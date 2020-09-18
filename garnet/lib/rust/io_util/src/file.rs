// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utility functions for fuchsia.io files.

use {
    crate::node::{self, CloseError, OpenError},
    anyhow::Error,
    fidl::encoding::{decode_persistent, encode_persistent, Decodable, Encodable},
    fidl_fuchsia_io::{FileMarker, FileProxy, MAX_BUF},
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_zircon::{Duration, Status},
    thiserror::Error,
};

mod async_reader;
pub use async_reader::AsyncReader;

/// An error encountered while reading a file
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ReadError {
    #[error("while opening the file: {0}")]
    Open(#[from] OpenError),

    #[error("read call failed: {0}")]
    Fidl(#[from] fidl::Error),

    #[error("read failed with status: {0}")]
    ReadError(#[source] Status),

    #[error("file was not a utf-8 encoded string: {0}")]
    InvalidUtf8(#[from] std::string::FromUtf8Error),

    #[error("read timed out")]
    Timeout,
}

/// An error encountered while reading a named file
#[derive(Debug, Error)]
#[error("error reading '{path}': {source}")]
pub struct ReadNamedError {
    path: String,

    #[source]
    source: ReadError,
}

impl ReadNamedError {
    /// Returns the path associated with this error.
    pub fn path(&self) -> &str {
        &self.path
    }

    /// Unwraps the inner read error, discarding the associated path.
    pub fn into_inner(self) -> ReadError {
        self.source
    }
}

/// An error encountered while writing a file
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum WriteError {
    #[error("while creating the file: {0}")]
    Create(#[from] OpenError),

    #[error("write call failed: {0}")]
    Fidl(#[from] fidl::Error),

    #[error("write failed with status: {0}")]
    WriteError(#[source] Status),

    #[error("file endpoint reported more bytes written than were provided")]
    Overwrite,
}

/// An error encountered while writing a named file
#[derive(Debug, Error)]
#[error("error writing '{path}': {source}")]
pub struct WriteNamedError {
    path: String,

    #[source]
    source: WriteError,
}

impl WriteNamedError {
    /// Returns the path associated with this error.
    pub fn path(&self) -> &str {
        &self.path
    }

    /// Unwraps the inner write error, discarding the associated path.
    pub fn into_inner(self) -> WriteError {
        self.source
    }
}

/// Opens the given `path` from the current namespace as a [`FileProxy`]. The target is not
/// verified to be any particular type and may not implement the fuchsia.io.File protocol.
pub fn open_in_namespace(path: &str, flags: u32) -> Result<FileProxy, OpenError> {
    let (dir, server_end) =
        fidl::endpoints::create_proxy::<FileMarker>().map_err(OpenError::CreateProxy)?;

    node::connect_in_namespace(path, flags, server_end.into_channel())
        .map_err(OpenError::Namespace)?;

    Ok(dir)
}

/// Gracefully closes the file proxy from the remote end.
pub async fn close(file: FileProxy) -> Result<(), CloseError> {
    let status = file.close().await.map_err(CloseError::SendCloseRequest)?;
    Status::ok(status).map_err(CloseError::CloseError)
}

/// Write the given data into a file at `path` in the current namespace. The path must be an
/// absolute path.
/// * If the file already exists, replaces existing contents.
/// * If the file does not exist, creates the file.
pub async fn write_in_namespace<D>(path: &str, data: D) -> Result<(), WriteNamedError>
where
    D: AsRef<[u8]>,
{
    async {
        let flags = fidl_fuchsia_io::OPEN_RIGHT_WRITABLE
            | fidl_fuchsia_io::OPEN_FLAG_CREATE
            | fidl_fuchsia_io::OPEN_FLAG_TRUNCATE;
        let file = open_in_namespace(path, flags)?;

        write(&file, data).await?;

        let _ = close(file).await;
        Ok(())
    }
    .await
    .map_err(|source| WriteNamedError { path: path.to_owned(), source })
}

/// Writes the given data into the given file.
pub async fn write<D>(file: &FileProxy, data: D) -> Result<(), WriteError>
where
    D: AsRef<[u8]>,
{
    let mut data = data.as_ref();

    while !data.is_empty() {
        let (status, bytes_written) =
            file.write(&data[..std::cmp::min(MAX_BUF as usize, data.len())]).await?;
        Status::ok(status).map_err(WriteError::WriteError)?;

        if bytes_written > data.len() as u64 {
            return Err(WriteError::Overwrite);
        }

        data = &data[bytes_written as usize..];
    }
    Ok(())
}

/// Write the given FIDL message in a binary form into a file open for writing.
pub async fn write_fidl<T: Encodable>(file: &FileProxy, data: &mut T) -> Result<(), Error> {
    write(file, encode_persistent(data)?).await?;
    Ok(())
}

/// Write the given FIDL encoded message into a file at `path`. The path must be an absolute path.
/// * If the file already exists, replaces existing contents.
/// * If the file does not exist, creates the file.
pub async fn write_fidl_in_namespace<T: Encodable>(path: &str, data: &mut T) -> Result<(), Error> {
    write_in_namespace(path, encode_persistent(data)?).await?;
    Ok(())
}

/// Reads all data from the given file's current offset to the end of the file.
pub async fn read(file: &FileProxy) -> Result<Vec<u8>, ReadError> {
    let mut out = Vec::new();

    loop {
        let (status, mut bytes) = file.read(MAX_BUF).await?;
        Status::ok(status).map_err(ReadError::ReadError)?;

        if bytes.is_empty() {
            break;
        }
        out.append(&mut bytes);
    }
    Ok(out)
}

/// Reads all data from the file at `path` in the current namespace. The path must be an absolute
/// path.
pub async fn read_in_namespace(path: &str) -> Result<Vec<u8>, ReadNamedError> {
    async {
        let file = open_in_namespace(path, fidl_fuchsia_io::OPEN_RIGHT_READABLE)?;
        read(&file).await
    }
    .await
    .map_err(|source| ReadNamedError { path: path.to_owned(), source })
}

/// Reads a utf-8 encoded string from the given file's current offset to the end of the file.
pub async fn read_to_string(file: &FileProxy) -> Result<String, ReadError> {
    let bytes = read(file).await?;
    let string = String::from_utf8(bytes)?;
    Ok(string)
}

/// Reads a utf-8 encoded string from the file at `path` in the current namespace. The path must be
/// an absolute path.
pub async fn read_in_namespace_to_string(path: &str) -> Result<String, ReadNamedError> {
    let bytes = read_in_namespace(path).await?;
    let string = String::from_utf8(bytes)
        .map_err(|source| ReadNamedError { path: path.to_owned(), source: source.into() })?;
    Ok(string)
}

/// Reads a utf-8 encoded string from the file at `path` in the current namespace. The path must be
/// an absolute path. Times out if the read takes longer than the given `timeout` duration.
pub async fn read_in_namespace_to_string_with_timeout(
    path: &str,
    timeout: Duration,
) -> Result<String, ReadNamedError> {
    read_in_namespace_to_string(&path)
        .on_timeout(timeout.after_now(), || {
            Err(ReadNamedError { path: path.to_owned(), source: ReadError::Timeout })
        })
        .await
}

/// Read the given FIDL message from binary form from a file open for reading.
/// FIDL structure should be provided at a read time.
/// Incompatible data is populated as per FIDL ABI compatibility guide:
/// https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/abi-compat
pub async fn read_fidl<T: Decodable>(file: &FileProxy) -> Result<T, Error> {
    let bytes = read(file).await?;
    Ok(decode_persistent(&bytes)?)
}

/// Read the given FIDL message from binary file at `path` in the current namespace. The path
/// must be an absolute path.
/// FIDL structure should be provided at a read time.
/// Incompatible data is populated as per FIDL ABI compatibility guide:
/// https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/abi-compat
pub async fn read_in_namespace_to_fidl<T: Decodable>(path: &str) -> Result<T, Error> {
    let bytes = read_in_namespace(path).await?;
    Ok(decode_persistent(&bytes)?)
}

#[cfg(test)]
mod tests {

    use {
        super::*,
        crate::{directory, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
        fidl::fidl_table,
        fuchsia_async as fasync,
        matches::assert_matches,
        std::path::Path,
        tempfile::TempDir,
    };

    const DATA_FILE_CONTENTS: &str = "Hello World!\n";

    // open_in_namespace

    #[fasync::run_singlethreaded(test)]
    async fn open_in_namespace_opens_real_file() {
        let exists = open_in_namespace("/pkg/data/file", OPEN_RIGHT_READABLE).unwrap();
        assert_matches!(close(exists).await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_in_namespace_opens_fake_file_under_of_root_namespace_entry() {
        let notfound = open_in_namespace("/pkg/fake", OPEN_RIGHT_READABLE).unwrap();
        // The open error is not detected until the proxy is interacted with.
        assert_matches!(close(notfound).await, Err(_));
    }

    #[fasync::run_singlethreaded(test)]
    async fn open_in_namespace_rejects_fake_root_namespace_entry() {
        assert_matches!(
            open_in_namespace("/fake", OPEN_RIGHT_READABLE),
            Err(OpenError::Namespace(Status::NOT_FOUND))
        );
    }

    // write_in_namespace

    #[fasync::run_singlethreaded(test)]
    async fn write_in_namespace_creates_file() {
        let tempdir = TempDir::new().unwrap();
        let path = tempdir.path().join(Path::new("new-file")).to_str().unwrap().to_owned();

        // Write contents.
        let data = b"\x80"; // Non UTF-8 data: a continuation byte as the first byte.
        write_in_namespace(&path, data).await.unwrap();

        // Verify contents.
        let contents = std::fs::read(&path).unwrap();
        assert_eq!(&contents, &data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_in_namespace_overwrites_existing_file() {
        let tempdir = TempDir::new().unwrap();
        let path = tempdir.path().join(Path::new("existing-file")).to_str().unwrap().to_owned();

        // Write contents.
        let original_data = b"\x80\x81"; // Non UTF-8 data: a continuation byte as the first byte.
        write_in_namespace(&path, original_data).await.unwrap();

        // Over-write contents.
        let new_data = b"\x82"; // Non UTF-8 data: a continuation byte as the first byte.
        write_in_namespace(&path, new_data).await.unwrap();

        // Verify contents.
        let contents = std::fs::read(&path).unwrap();
        assert_eq!(&contents, &new_data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_in_namespace_fails_on_invalid_namespace_entry() {
        assert_matches!(
            write_in_namespace("/fake", b"").await,
            Err(WriteNamedError { path, source: WriteError::Create(_) }) if path == "/fake"
        );
        let err = write_in_namespace("/fake", b"").await.unwrap_err();
        assert_eq!(err.path(), "/fake");
        assert_matches!(err.into_inner(), WriteError::Create(_));
    }

    // write

    #[fasync::run_singlethreaded(test)]
    async fn write_writes_to_file() {
        let tempdir = TempDir::new().unwrap();
        let dir = directory::open_in_namespace(
            tempdir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .unwrap();

        // Write contents.
        let flags = fidl_fuchsia_io::OPEN_RIGHT_WRITABLE | fidl_fuchsia_io::OPEN_FLAG_CREATE;
        let file = directory::open_file(&dir, "file", flags).await.unwrap();
        let data = b"\x80"; // Non UTF-8 data: a continuation byte as the first byte.
        write(&file, data).await.unwrap();

        // Verify contents.
        let contents = std::fs::read(tempdir.path().join(Path::new("file"))).unwrap();
        assert_eq!(&contents, &data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_writes_to_file_in_chunks_if_needed() {
        let tempdir = TempDir::new().unwrap();
        let dir = directory::open_in_namespace(
            tempdir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .unwrap();

        // Write contents.
        let flags = fidl_fuchsia_io::OPEN_RIGHT_WRITABLE | fidl_fuchsia_io::OPEN_FLAG_CREATE;
        let file = directory::open_file(&dir, "file", flags).await.unwrap();
        let data = "abc".repeat(10000);
        write(&file, &data).await.unwrap();

        // Verify contents.
        let contents = std::fs::read_to_string(tempdir.path().join(Path::new("file"))).unwrap();
        assert_eq!(&contents, &data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn write_appends_to_file() {
        let tempdir = TempDir::new().unwrap();
        let dir = directory::open_in_namespace(
            tempdir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .unwrap();

        // Create and write to the file.
        let flags = fidl_fuchsia_io::OPEN_RIGHT_WRITABLE | fidl_fuchsia_io::OPEN_FLAG_CREATE;
        let file = directory::open_file(&dir, "file", flags).await.unwrap();
        write(&file, "Hello ").await.unwrap();
        write(&file, "World!\n").await.unwrap();
        close(file).await.unwrap();

        // Verify contents.
        let contents = std::fs::read(tempdir.path().join(Path::new("file"))).unwrap();
        assert_eq!(&contents[..], DATA_FILE_CONTENTS.as_bytes());
    }

    // read

    #[fasync::run_singlethreaded(test)]
    async fn read_reads_to_end_of_file() {
        let file = open_in_namespace("/pkg/data/file", OPEN_RIGHT_READABLE).unwrap();

        let contents = read(&file).await.unwrap();
        assert_eq!(&contents[..], DATA_FILE_CONTENTS.as_bytes());
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_reads_from_current_position() {
        let file = open_in_namespace("/pkg/data/file", OPEN_RIGHT_READABLE).unwrap();

        // Advance past the first byte.
        file.read(1).await.unwrap();

        // Verify the rest of the file is read.
        let contents = read(&file).await.unwrap();
        assert_eq!(&contents[..], "ello World!\n".as_bytes());
    }

    // read_in_namespace

    #[fasync::run_singlethreaded(test)]
    async fn read_in_namespace_reads_contents() {
        let contents = read_in_namespace("/pkg/data/file").await.unwrap();
        assert_eq!(&contents[..], DATA_FILE_CONTENTS.as_bytes());
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_in_namespace_fails_on_invalid_namespace_entry() {
        assert_matches!(
            read_in_namespace("/fake").await,
            Err(ReadNamedError { path, source: ReadError::Open(_) }) if path == "/fake"
        );
        let err = read_in_namespace("/fake").await.unwrap_err();
        assert_eq!(err.path(), "/fake");
        assert_matches!(err.into_inner(), ReadError::Open(_));
    }

    // read_to_string

    #[fasync::run_singlethreaded(test)]
    async fn read_to_string_reads_data_file() {
        let file = open_in_namespace("/pkg/data/file", OPEN_RIGHT_READABLE).unwrap();
        assert_eq!(read_to_string(&file).await.unwrap(), DATA_FILE_CONTENTS);
    }

    // read_in_namespace_to_string

    #[fasync::run_singlethreaded(test)]
    async fn read_in_namespace_to_string_reads_data_file() {
        assert_eq!(
            read_in_namespace_to_string("/pkg/data/file").await.unwrap(),
            DATA_FILE_CONTENTS
        );
    }

    // write_fidl

    #[fasync::run_singlethreaded(test)]
    async fn write_fidl_writes_to_file() {
        struct DataTable {
            num: Option<i32>,
            string: Option<String>,
        }

        fidl_table! {
            name: DataTable,
            members: [
                num {
                    ty: i32,
                    ordinal: 1,
                },
                string {
                    ty: String,
                    ordinal: 2,
                },
            ],
        }

        let tempdir = TempDir::new().unwrap();
        let dir = directory::open_in_namespace(
            tempdir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .unwrap();

        // Write contents.
        let flags = fidl_fuchsia_io::OPEN_RIGHT_WRITABLE | fidl_fuchsia_io::OPEN_FLAG_CREATE;
        let file = directory::open_file(&dir, "file", flags).await.unwrap();

        let mut data = DataTable { num: Some(42), string: Some(DATA_FILE_CONTENTS.to_string()) };

        // Binary encoded FIDL message, with header and padding.
        let fidl_bytes = encode_persistent(&mut data).unwrap();

        write_fidl(&file, &mut data).await.unwrap();

        // Verify contents.
        let contents = std::fs::read(tempdir.path().join(Path::new("file"))).unwrap();
        assert_eq!(&contents, &fidl_bytes);
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_fidl_reads_from_file() {
        #[derive(PartialEq, Eq, Debug)]
        struct DataTable {
            num: Option<i32>,
            string: Option<String>,
            new_field: Option<String>,
        }

        fidl_table! {
            name: DataTable,
            members: [
                num {
                    ty: i32,
                    ordinal: 1,
                },
                string {
                    ty: String,
                    ordinal: 2,
                },
                new_field {
                    ty: String,
                    ordinal: 3,
                },
            ],
        }

        let file = open_in_namespace("/pkg/data/fidl_file", OPEN_RIGHT_READABLE).unwrap();

        let contents = read_fidl::<DataTable>(&file).await.unwrap();

        let data = DataTable {
            num: Some(42),
            string: Some(DATA_FILE_CONTENTS.to_string()),
            new_field: None,
        };
        assert_eq!(&contents, &data);
    }
}
