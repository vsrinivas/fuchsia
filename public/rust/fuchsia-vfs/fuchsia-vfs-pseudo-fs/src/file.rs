// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Specific implementation of a pseudo file trait backed by read and/or write callbacks and a
//! buffer.
//!
//! Read callback, if any, is called when the connection to the file is first opened and
//! pre-populates a buffer that will be used to when serving this file content over this particular
//! connection.  Read callback is ever called once.
//!
//! Write callback, if any, is called when the connection is closed if the file content was ever
//! modified while the connection was open.  Modifications are: write() calls or opening a file for
//! writing with the OPEN_FLAG_TRUNCATE flag set.
//!
//! First write operation will reset the seek position and empty the buffer.  Any subsequent read
//! operations will read from the same buffer, returning the data that was already written.  This
//! quirk in behaviour is in order to simplify the read()/write() scenarios.
//!
//! Main use case for the pseudo files that are both readable and writeable is the exposure of the
//! component configuration parameters.  In this case when the configuration value is read it is
//! always presented in a canonical format, but when it is written, multiple formats could be used.
//! As a consequence, when the new value length could shorter than the current value (and when
//! different formats are supported it is more likely), besides just writing new file content one
//! would also need to truncate the file.  As we treat our configuration values as atomic, we
//! consider the scenario of an incremental edit less likely, so we truncate automatically.
//!
//! Thinking again about the above, it seems like an unnecessary complication.  When someone wants
//! to just update the value, they can open with OPEN_FLAG_TRUNCATE flag (as does shell when using
//! output redirection) and when the scenario is to read and to write, there is a Truncate() call
//! that sets new file size.  This would remove the quirk and make the file behave more like all
//! the other files.

#![warn(missing_docs)]

use {
    crate::PseudoFile,
    failure::Error,
    fidl::encoding::OutOfLine,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_io::{
        FileObject, FileRequest, FileRequestStream, NodeAttributes, NodeInfo, SeekOrigin,
        INO_UNKNOWN, MODE_PROTECTION_MASK, MODE_TYPE_FILE, OPEN_FLAG_APPEND, OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_DIRECTORY, OPEN_FLAG_STATUS, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fuchsia_async::Channel,
    fuchsia_zircon::{
        sys::{ZX_ERR_NOT_SUPPORTED, ZX_OK},
        Status,
    },
    futures::{
        future::FusedFuture,
        stream::{FuturesUnordered, Stream, StreamExt, StreamFuture},
        task::LocalWaker,
        Future, Poll,
    },
    libc::{S_IRUSR, S_IWUSR},
    std::{
        io::Write,
        iter::ExactSizeIterator,
        mem,
        pin::{Pin, Unpin},
    },
};

// TODO: When trait aliases are implemented (rust-lang/rfcs#1733)
// trait OnReadHandler = FnMut() -> Result<Vec<u8>, Status>;
// trait OnWriteHandler = FnMut(Vec<u8>) -> Result<(), Status>;

/// POSIX emulation layer access attributes set by default for files created with read_only().
pub const DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES: u32 = S_IRUSR;

/// Creates a new read-only `PseudoFile` backed by the specified read handler.
///
/// The handler is called every time a read operation is performed on the file.  It is only allowed
/// to read at offset 0, and all of the content returned by the handler is returned by the read
/// operation.  Subsequent reads act the same - there is no seek position, nor ability to read
/// content in chunks.
pub fn read_only<OnRead>(on_read: OnRead) -> impl PseudoFile
where
    OnRead: FnMut() -> Result<Vec<u8>, Status>,
{
    read_only_attr(on_read, DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES)
}

/// See [`read_only()`].  Wraps the callback, allowing it to return a String instead of a Vec<u8>,
/// but otherwise behaves identical to #read_only().
pub fn read_only_str<OnReadStr>(mut on_read: OnReadStr) -> impl PseudoFile
where
    OnReadStr: FnMut() -> Result<String, Status>,
{
    PseudoFileImpl::<_, fn(Vec<u8>) -> Result<(), Status>>::new(
        Some(move || on_read().map(|content| content.into_bytes())),
        0,
        None,
        DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES,
    )
}

/// Same as [`read_only()`] but also allows to select custom attributes for the POSIX emulation
/// layer.  Note that only the MODE_PROTECTION_MASK part of the protection_attributes argument will
/// be stored.
pub fn read_only_attr<OnRead>(on_read: OnRead, protection_attributes: u32) -> impl PseudoFile
where
    OnRead: FnMut() -> Result<Vec<u8>, Status>,
{
    PseudoFileImpl::<_, fn(Vec<u8>) -> Result<(), Status>>::new(
        Some(on_read),
        0,
        None,
        protection_attributes & MODE_PROTECTION_MASK,
    )
}

/// POSIX emulation layer access attributes set by default for files created with write_only().
pub const DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES: u32 = S_IWUSR;

/// Creates a new write-only `PseudoFile` backed by the specified write handler.
///
/// The handler is called every time a write operation is performed on the file.  It is only
/// allowed to write at offset 0, and all of the new content should be provided to a single write
/// operation.  Subsequent writes act the same - there is no seek position, nor ability to write
/// content in chunks.
pub fn write_only<OnWrite>(capacity: u64, on_write: OnWrite) -> impl PseudoFile
where
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status>,
{
    write_only_attr(capacity, on_write, DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES)
}

/// See [`write_only()`].  Only allows valid UTF-8 content to be written into the file.  Written
/// bytes are converted into a string instance an std::str::from_utf8() call, and the passed to the
/// handler.
pub fn write_only_str<OnWriteStr>(capacity: u64, mut on_write: OnWriteStr) -> impl PseudoFile
where
    OnWriteStr: FnMut(String) -> Result<(), Status>,
{
    PseudoFileImpl::<fn() -> Result<Vec<u8>, Status>, _>::new(
        None,
        capacity,
        Some(move |bytes: Vec<u8>| match String::from_utf8(bytes) {
            Ok(content) => on_write(content),
            Err(_) => Err(Status::INVALID_ARGS),
        }),
        DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES,
    )
}

/// Same as [`write_only()`] but also allows to select custom attributes for the POSIX emulation
/// layer.  Note that only the MODE_PROTECTION_MASK part of the protection_attributes argument will
/// be stored.
pub fn write_only_attr<OnWrite>(
    capacity: u64, on_write: OnWrite, protection_attributes: u32,
) -> impl PseudoFile
where
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status>,
{
    PseudoFileImpl::<fn() -> Result<Vec<u8>, Status>, _>::new(
        None,
        capacity,
        Some(on_write),
        protection_attributes & MODE_PROTECTION_MASK,
    )
}

/// POSIX emulation layer access attributes set by default for files created with read_write().
pub const DEFAULT_READ_WRITE_PROTECTION_ATTRIBUTES: u32 =
    DEFAULT_READ_ONLY_PROTECTION_ATTRIBUTES | DEFAULT_WRITE_ONLY_PROTECTION_ATTRIBUTES;

/// Creates new `PseudoFile` backed by the specified read and write handlers.
///
/// The read handler is called every time a read operation is performed on the file.  It is only
/// allowed to read at offset 0, and all of the content returned by the handler is returned by the
/// read operation.  Subsequent reads act the same - there is no seek position, nor ability to read
/// content in chunks.
///
/// The write handler is called every time a write operation is performed on the file.  It is only
/// allowed to write at offset 0, and all of the new content should be provided to a single write
/// operation.  Subsequent writes act the same - there is no seek position, nor ability to write
/// content in chunks.
pub fn read_write<OnRead, OnWrite>(
    on_read: OnRead, capacity: u64, on_write: OnWrite,
) -> impl PseudoFile
where
    OnRead: FnMut() -> Result<Vec<u8>, Status>,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status>,
{
    read_write_attr(
        on_read,
        capacity,
        on_write,
        DEFAULT_READ_WRITE_PROTECTION_ATTRIBUTES,
    )
}

/// See [`read_write()`].  Wraps the read callback, allowing it to return a [`String`] instead of a
/// [`Vec<u8>`].  Wraps the write callback, only allowing valid UTF-8 content to be written into
/// the file.  Written bytes are converted into a string instance an [`std::str::from_utf8()`]
/// call, and the passed to the handler.
/// In every other aspect behaves just like [`read_write()`].
pub fn read_write_str<OnReadStr, OnWriteStr>(
    mut on_read: OnReadStr, capacity: u64, mut on_write: OnWriteStr,
) -> impl PseudoFile
where
    OnReadStr: FnMut() -> Result<String, Status>,
    OnWriteStr: FnMut(String) -> Result<(), Status>,
{
    PseudoFileImpl::new(
        Some(move || on_read().map(|content| content.into_bytes())),
        capacity,
        Some(move |bytes: Vec<u8>| match String::from_utf8(bytes) {
            Ok(content) => on_write(content),
            Err(_) => Err(Status::INVALID_ARGS),
        }),
        DEFAULT_READ_WRITE_PROTECTION_ATTRIBUTES,
    )
}

/// Same as [`read_write()`] but also allows to select custom attributes for the POSIX emulation
/// layer.  Note that only the MODE_PROTECTION_MASK part of the protection_attributes argument will
/// be stored.
pub fn read_write_attr<OnRead, OnWrite>(
    on_read: OnRead, capacity: u64, on_write: OnWrite, protection_attributes: u32,
) -> impl PseudoFile
where
    OnRead: FnMut() -> Result<Vec<u8>, Status>,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status>,
{
    PseudoFileImpl::new(
        Some(on_read),
        capacity,
        Some(on_write),
        protection_attributes & MODE_PROTECTION_MASK,
    )
}

struct FileConnection {
    requests: FileRequestStream,
    /// Either the "flags" value passed into [`PseudoFile::add_request_stream()`], or the "flags"
    /// value passed into FileRequest::Clone().
    flags: u32,
    /// Seek position.  Next byte to be read or written within the buffer.  This might be beyond
    /// the current size of buffer, matching POSIX:
    ///
    ///     http://pubs.opengroup.org/onlinepubs/9699919799/functions/lseek.html
    ///
    /// It will cause the buffern to be extended with zeroes (if necessary) when write() is called.
    // While the content in the buffer vector uses usize for the size, it is easier to use u64 to
    // match the FIDL bindings API.  Pseudo files are not expected to cross the 2^64 bytes size
    // limit.  And all the code is much simpler when we just assume that usize is the same as u64.
    // Should we need to port to a 128 bit platform, there are static assertions in the code that
    // would fail.
    seek: u64,
    /// Per connection buffer.  See module documentation for details.
    buffer: Vec<u8>,
    /// Starts as false, and causes the [`on_write()`] to be called when the connection is closed
    /// if set to true during the lifetime of the connection.
    was_written: bool,
}

impl FileConnection {
    /// Creates a new [`FileConnection`] instance, immediately wrapping it in a [`StreamFuture`].
    /// This is how [`FileConnection`]s are used in the pseudo file implementation.
    fn as_stream_future(
        requests: FileRequestStream, flags: u32, buffer: Vec<u8>, was_written: bool,
    ) -> StreamFuture<FileConnection> {
        (FileConnection {
            requests,
            flags,
            seek: 0,
            buffer,
            was_written,
        })
        .into_future()
    }
}

/// Allow [`FileConnection`] to be wrapped in a [`StreamFuture`], to be further contained inside
/// [`FuturesUnordered`].
impl Stream for FileConnection {
    // We are just proxying the FileRequestStream requests.
    type Item = <FileRequestStream as Stream>::Item;

    fn poll_next(mut self: Pin<&mut Self>, lw: &LocalWaker) -> Poll<Option<Self::Item>> {
        self.requests.poll_next_unpin(lw)
    }
}

struct PseudoFileImpl<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status>,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status>,
{
    /// A handler to be invoked to populate the content buffer when a connection to this file is
    /// opened.
    on_read: Option<OnRead>,

    /// Maximum size the buffer that holds the value written into this file can grow to.  When the
    /// buffer is populated by a the [`on_read`] handler, this restriction is not enforced.  The
    /// maximum size of the buffer passed into [`on_write`] is the maximum of the size of the
    /// buffer that [`on_read`] have returnd and this value.
    capacity: u64,

    /// A handler to be invoked to "update" the file content, if it was modified during a
    /// connection lifetime.
    on_write: Option<OnWrite>,

    /// MODE_PROTECTION_MASK attributes returned by this file through io.fild:Node::GetAttr.  They
    /// have no meaning for the file operation itself, but may have consequences to the POSIX
    /// emulation layer - for example, it makes sense to remove the read flags from a read-only
    /// file.  This filed should only have bits in the MODE_PROTECTION_MASK part set.
    protection_attributes: u32,

    /// All the currently open connections for this file.
    connections: FuturesUnordered<StreamFuture<FileConnection>>,
}

/// Return type for PseudoFileImpl::handle_request().
enum ConnectionState {
    Alive,
    Closed,
}

/// We assume that usize/isize and u64/i64 are of the same size in a few locations in code.  This
/// macro is used to mark the locations of those assumptions.
/// Copied from
///
///     https://docs.rs/static_assertions/0.2.5/static_assertions/macro.assert_eq_size.html
///
macro_rules! assert_eq_size {
    ($x:ty, $($xs:ty),+ $(,)*) => {
        $(let _ = core::mem::transmute::<$x, $xs>;)+
    };
}

impl<OnRead, OnWrite> PseudoFileImpl<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status>,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status>,
{
    fn new(
        on_read: Option<OnRead>, capacity: u64, on_write: Option<OnWrite>,
        protection_attributes: u32,
    ) -> Self {
        PseudoFileImpl {
            on_read,
            capacity,
            on_write,
            protection_attributes,
            connections: FuturesUnordered::new(),
        }
    }

    fn validate_flags(&mut self, parent_flags: u32, flags: u32) -> Result<(), Status> {
        if flags & OPEN_FLAG_DIRECTORY != 0 {
            return Err(Status::NOT_DIR);
        }

        let allowed_flags = OPEN_FLAG_STATUS
            | OPEN_FLAG_DESCRIBE
            | if self.on_read.is_some() {
                OPEN_RIGHT_READABLE
            } else {
                0
            }
            | if self.on_write.is_some() {
                OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE
            } else {
                0
            };

        let prohibited_flags = (0 | if self.on_read.is_some() {
                OPEN_FLAG_TRUNCATE
            } else {
                0
            } | if self.on_write.is_some() {
                OPEN_FLAG_APPEND
            } else {
                0
            })
            // allowed_flags takes precedence over prohibited_flags.
            & !allowed_flags;

        if flags & OPEN_RIGHT_READABLE != 0 && parent_flags & OPEN_RIGHT_READABLE == 0 {
            return Err(Status::ACCESS_DENIED);
        }

        if flags & OPEN_RIGHT_WRITABLE != 0 && parent_flags & OPEN_RIGHT_WRITABLE == 0 {
            return Err(Status::ACCESS_DENIED);
        }

        if flags & prohibited_flags != 0 {
            return Err(Status::INVALID_ARGS);
        }

        if flags & !allowed_flags != 0 {
            return Err(Status::NOT_SUPPORTED);
        }

        Ok(())
    }

    fn add_request_stream_clone(
        &mut self, parent: &FileConnection, flags: u32, request_stream: FileRequestStream,
    ) -> Status {
        if let Err(status) = self.validate_flags(parent.flags, flags) {
            return status;
        }

        match self.init_buffer(flags) {
            Ok((buffer, was_written)) => {
                let conn =
                    FileConnection::as_stream_future(request_stream, flags, buffer, was_written);
                self.connections.push(conn);
                Status::OK
            }
            Err(status) => status,
        }
    }

    fn handle_request(
        &mut self, req: FileRequest, connection: &mut FileConnection,
    ) -> Result<ConnectionState, Error> {
        match req {
            FileRequest::Clone {
                flags,
                object,
                control_handle: _,
            } => {
                // TODO: I would like to do this:
                //
                // let (clone_stream, clone_control_handle) =
                // object.into_stream_and_control_handle()?;
                //
                // But it does not work, as the clone_stream is an instance of the
                // NodeRequestStream instead of the FileRequestStream, due to the fact that
                // "object" is a fidl::endpoints::ServerEnd<NodeMarker>, not a
                // fidl::endpoints::ServerEnd<FileMarker>.  Seems like the issues goes deep into
                // FIDL language itself, as it does not seem to support override information for
                // derived interfaces.  This is already a FileRequest::Clone, but the argument is a
                // NodeMarker.
                let clone_stream =
                    FileRequestStream::from_channel(Channel::from_channel(object.into_channel())?);
                // Need to hold a reference to the underlying channel in case addition fails - we
                // still send OnOpen() if it was initially requested.
                let clone_control_handle = clone_stream.control_handle();
                let status = self.add_request_stream_clone(connection, flags, clone_stream);
                if flags & OPEN_FLAG_STATUS != 0 {
                    let mut info = NodeInfo::File(FileObject { event: None });
                    clone_control_handle.send_on_open_(
                        status.into_raw(),
                        if flags & OPEN_FLAG_DESCRIBE != 0 {
                            Some(OutOfLine(&mut info))
                        } else {
                            None
                        },
                    )?;
                }
            }
            FileRequest::Close { responder } => {
                self.handle_close(connection, |status| responder.send(status.into_raw()))?;
                return Ok(ConnectionState::Closed);
            }
            FileRequest::Describe { responder } => {
                let mut info = NodeInfo::File(FileObject { event: None });
                responder.send(&mut info)?;
            }
            FileRequest::Sync { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::GetAttr { responder } => {
                let mut attrs = NodeAttributes {
                    mode: MODE_TYPE_FILE | self.protection_attributes,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                };
                responder.send(ZX_OK, &mut attrs)?;
            }
            FileRequest::SetAttr {
                flags: _,
                attributes: _,
                responder,
            } => {
                // According to zircon/system/fidl/fuchsia-io/io.fidl the only flag that might be
                // modified through this call is OPEN_FLAG_APPEND, and it is not supported by the
                // PseudoFileImpl.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::Ioctl {
                opcode: _,
                max_out: _,
                handles: _,
                in_: _,
                responder,
            } => {
                responder.send(
                    ZX_ERR_NOT_SUPPORTED,
                    &mut std::iter::empty(),
                    &mut std::iter::empty(),
                )?;
            }
            FileRequest::Read { count, responder } => {
                let actual =
                    self.handle_read(connection, connection.seek, count, |status, content| {
                        responder.send(status.into_raw(), content)
                    })?;
                connection.seek += actual;
            }
            FileRequest::ReadAt {
                offset,
                count,
                responder,
            } => {
                self.handle_read(connection, offset, count, |status, content| {
                    responder.send(status.into_raw(), content)
                })?;
            }
            FileRequest::Write { data, responder } => {
                let actual =
                    self.handle_write(connection, connection.seek, data, |status, actual| {
                        responder.send(status.into_raw(), actual)
                    })?;
                connection.seek += actual;
            }
            FileRequest::WriteAt {
                offset,
                data,
                responder,
            } => {
                self.handle_write(connection, offset, data, |status, actual| {
                    // Seems like our API is not really designed for 128 bit machines. If data
                    // contains more than 16EB, we may not be returning the correct number here.
                    responder.send(status.into_raw(), actual as u64)
                })?;
            }
            FileRequest::Seek {
                offset,
                start,
                responder,
            } => {
                self.handle_seek(connection, offset, start, |status, offset| {
                    responder.send(status.into_raw(), offset)
                })?;
            }
            FileRequest::Truncate { length, responder } => {
                self.handle_truncate(connection, length, |status| {
                    responder.send(status.into_raw())
                })?;
            }
            FileRequest::GetFlags { responder } => {
                responder.send(ZX_OK, connection.flags)?;
            }
            FileRequest::SetFlags {
                flags: _,
                responder,
            } => {
                // TODO: Support OPEN_FLAG_APPEND?  It is the only flag that is allowed to be set
                // via this call according to the io.fidl.  It would be nice to have that
                // explicitly encoded in the API instead, I guess.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::GetVmo {
                flags: _,
                responder,
            } => {
                // There is no backing VMO.
                responder.send(ZX_OK, None)?;
            }
        }
        Ok(ConnectionState::Alive)
    }

    fn init_buffer(&mut self, flags: u32) -> Result<(Vec<u8>, bool), Status> {
        // No point in calling the read handler for non-readable files.
        if flags & OPEN_RIGHT_READABLE == 0 {
            return Ok((vec![], false));
        }

        match self.on_read {
            None => Ok((vec![], false)),
            Some(ref mut on_read) => {
                // No point in calling the read hander, if we want to erase all of the content
                // right away, but we need to remember the content was overwritten.
                if flags & OPEN_FLAG_TRUNCATE != 0 {
                    Ok((vec![], true))
                } else {
                    let buffer = on_read()?;
                    Ok((buffer, false))
                }
            }
        }
    }

    fn handle_read<R>(
        &mut self, connection: &FileConnection, offset: u64, mut count: u64, responder: R,
    ) -> Result<u64, fidl::Error>
    where
        R: FnOnce(Status, &mut ExactSizeIterator<Item = u8>) -> Result<(), fidl::Error>,
    {
        if connection.flags & OPEN_RIGHT_READABLE == 0 {
            responder(Status::ACCESS_DENIED, &mut std::iter::empty())?;
            return Ok(0);
        }

        match self.on_read {
            None => {
                responder(Status::NOT_SUPPORTED, &mut std::iter::empty())?;
                Ok(0)
            }
            Some(_) => {
                assert_eq_size!(usize, u64);

                let len = connection.buffer.len() as u64;

                if offset >= len {
                    responder(Status::OUT_OF_RANGE, &mut std::iter::empty())?;
                    return Ok(0);
                }

                count = core::cmp::min(count, len - offset);

                let from = offset as usize;
                let to = (offset + count) as usize;
                let mut content = connection.buffer[from..to].iter().cloned();
                responder(Status::OK, &mut content)?;
                Ok(count)
            }
        }
    }

    // Strictly speaking, we do not need to use a callback here, but we do need it in the
    // on_read() case above, so, for consistency, on_write() has the same interface.
    // TODO: Do I need to return the number of bytes written?
    fn handle_write<R>(
        &mut self, connection: &mut FileConnection, offset: u64, content: Vec<u8>, responder: R,
    ) -> Result<u64, fidl::Error>
    where
        R: FnOnce(Status, u64) -> Result<(), fidl::Error>,
    {
        if connection.flags & OPEN_RIGHT_WRITABLE == 0 {
            responder(Status::ACCESS_DENIED, 0)?;
            return Ok(0);
        }

        assert_eq_size!(usize, u64);
        let effective_capacity = core::cmp::max(connection.buffer.len() as u64, self.capacity);

        match self.on_write {
            None => {
                responder(Status::NOT_SUPPORTED, 0)?;
                Ok(0)
            }
            Some(_) if offset >= effective_capacity => {
                responder(Status::OUT_OF_RANGE, 0)?;
                Ok(0)
            }
            Some(_) => {
                assert_eq_size!(usize, u64);

                let actual = core::cmp::min(effective_capacity - offset, content.len() as u64);

                let buffer = &mut connection.buffer;

                if buffer.len() as u64 <= offset + actual {
                    buffer.resize((offset + actual) as usize, 0);
                }

                let from = offset as usize;
                let to = (offset + actual) as usize;
                let mut target = &mut buffer[from..to];
                let source = &content[0..actual as usize];
                target.write_all(source).unwrap();

                connection.was_written = true;

                responder(Status::OK, actual)?;
                Ok(actual)
            }
        }
    }

    fn handle_seek<R>(
        &mut self, connection: &mut FileConnection, offset: i64, start: SeekOrigin, responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, u64) -> Result<(), fidl::Error>,
    {
        let new_seek = match start {
            SeekOrigin::Start => offset as i128,

            SeekOrigin::Current => {
                assert_eq_size!(usize, i64);
                connection.seek as i128 + offset as i128
            }

            SeekOrigin::End => {
                assert_eq_size!(usize, i64, u64);
                connection.buffer.len() as i128 + offset as i128
            }
        };

        let effective_capacity =
            core::cmp::max(connection.buffer.len() as i128, self.capacity as i128);
        if new_seek < 0 || new_seek >= effective_capacity {
            responder(Status::OUT_OF_RANGE, connection.seek)?;
            return Ok(());
        }
        let new_seek = new_seek as u64;

        connection.seek = new_seek;
        responder(Status::OK, new_seek)
    }

    fn handle_truncate<R>(
        &mut self, connection: &mut FileConnection, length: u64, responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if connection.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::ACCESS_DENIED);
        }

        let effective_capacity = core::cmp::max(connection.buffer.len() as u64, self.capacity);

        match self.on_write {
            None => responder(Status::NOT_SUPPORTED),
            Some(_) if length > effective_capacity => responder(Status::OUT_OF_RANGE),
            Some(_) => {
                assert_eq_size!(usize, u64);

                connection.buffer.resize(length as usize, 0);

                // We are not supposed to touch the seek position during truncation, but the
                // effective_capacity may be smaller now - in which case we do need to move the
                // seek position.
                let new_effective_capacity = core::cmp::max(length, self.capacity);
                connection.seek = core::cmp::min(connection.seek, new_effective_capacity);

                responder(Status::OK)
            }
        }
    }

    fn handle_close<R>(
        &mut self, connection: &mut FileConnection, responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if !connection.was_written {
            return responder(Status::OK);
        }

        match self.on_write {
            None => responder(Status::OK),
            Some(ref mut on_write) => {
                match on_write(mem::replace(&mut connection.buffer, vec![])) {
                    Ok(()) => responder(Status::OK),
                    Err(status) => responder(status),
                }
            }
        }
    }
}

impl<OnRead, OnWrite> PseudoFile for PseudoFileImpl<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status>,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status>,
{
    fn add_request_stream(
        &mut self, flags: u32, request_stream: FileRequestStream,
    ) -> Result<(), Status> {
        self.validate_flags(!0, flags)?;
        let (buffer, was_written) = self.init_buffer(flags)?;
        let conn = FileConnection::as_stream_future(request_stream, flags, buffer, was_written);
        self.connections.push(conn);
        Ok(())
    }
}

impl<OnRead, OnWrite> Unpin for PseudoFileImpl<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status>,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status>,
{
}

impl<OnRead, OnWrite> FusedFuture for PseudoFileImpl<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status>,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status>,
{
    fn is_terminated(&self) -> bool {
        // The `PseudoFileImpl` never completes, but once there are no
        // more connections, it is blocked until more connections are
        // added. If the object currently polling a `PseudoFile` with
        // an empty set of connections is blocked on the `PseudoFile`
        // completing, it will never terminate.
        self.connections.len() == 0
    }
}

impl<OnRead, OnWrite> Future for PseudoFileImpl<OnRead, OnWrite>
where
    OnRead: FnMut() -> Result<Vec<u8>, Status>,
    OnWrite: FnMut(Vec<u8>) -> Result<(), Status>,
{
    type Output = Result<(), Error>;

    fn poll(mut self: Pin<&mut Self>, lw: &LocalWaker) -> Poll<Self::Output> {
        loop {
            match self.connections.poll_next_unpin(lw) {
                Poll::Ready(Some((maybe_request, mut connection))) => {
                    if let Some(Ok(request)) = maybe_request {
                        match self.handle_request(request, &mut connection) {
                            Ok(ConnectionState::Alive) => {
                                self.connections.push(connection.into_future())
                            }
                            Ok(ConnectionState::Closed) => (),
                            // An error occured while processing a request.  We will just close the
                            // connection, effectively closing the underlying channel in the
                            // desctructor.
                            _ => (),
                        }
                    }
                    // Similarly to the error that occures while handing a FIDL request, any
                    // connection level errors cause the connection to be closed.
                }
                // Even when we have no connections any more we still report Pending state, as we
                // may get more connections open in the future.
                Poll::Ready(None) | Poll::Pending => return Poll::Pending,
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use {
        fidl::endpoints::{create_endpoints, ServerEnd},
        fidl_fuchsia_io::{FileEvent, FileMarker, FileProxy, INO_UNKNOWN, MODE_TYPE_FILE},
        fuchsia_async as fasync,
        fuchsia_zircon::sys::ZX_ERR_ACCESS_DENIED,
        futures::channel::{mpsc, oneshot},
        futures::{select, Future, FutureExt, SinkExt},
        libc::{S_IRGRP, S_IROTH, S_IRUSR, S_IWGRP, S_IWOTH, S_IWUSR, S_IXGRP, S_IXOTH, S_IXUSR},
        pin_utils::pin_mut,
        std::cell::RefCell,
    };

    fn run_server_client<GetClientRes>(
        flags: u32, mut server: impl PseudoFile, get_client: impl FnOnce(FileProxy) -> GetClientRes,
    ) where
        GetClientRes: Future<Output = ()>,
    {
        let mut exec = fasync::Executor::new().expect("Executor creation failed");

        let (client_end, server_end) =
            create_endpoints::<FileMarker>().expect("Failed to create connection endpoints");

        server
            .add_request_stream(flags, server_end.into_stream().unwrap())
            .expect("add_request_stream() failed");

        let client_proxy = client_end.into_proxy().unwrap();
        let client = get_client(client_proxy);

        let future = server.join(client);
        // TODO: How to limit the execution time?  run_until_stalled() does not trigger timers, so
        // I can not do this:
        //
        //   let timeout = 300.millis();
        //   let future = future.on_timeout(
        //       timeout.after_now(),
        //       || panic!("Test did not finish in {}ms", timeout.millis()));

        // As our clients are async generators, we need to pin this future explicitly.
        // All async generators are !Unpin by default.
        pin_mut!(future);
        if let Poll::Ready((Err(e), ())) = exec.run_until_stalled(&mut future) {
            panic!("Server failed: {:?}", e);
        }
    }

    #[test]
    fn read_only_read() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| Ok(b"Read only test".to_vec())),
            async move |proxy| {
                assert_read!(proxy, "Read only test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_only_str_read() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_str(|| Ok(String::from("Read only str test"))),
            async move |proxy| {
                assert_read!(proxy, "Read only str test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_only_write() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(&*content, b"Write only test");
                Ok(())
            }),
            async move |proxy| {
                assert_write!(proxy, "Write only test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_only_str_write() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only_str(100, |content| {
                assert_eq!(&*content, "Write only test");
                Ok(())
            }),
            async move |proxy| {
                assert_write!(proxy, "Write only test");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_write_read_and_write() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Hello".to_vec()),
                100,
                |content| {
                    assert_eq!(*&content, b"Hello, world!");
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Hello");
                assert_write!(proxy, ", world!");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_write_str_read_and_write() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write_str(
                || Ok("Hello".to_string()),
                100,
                |content| {
                    assert_eq!(*&content, "Hello, world!");
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Hello");
                assert_write!(proxy, ", world!");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_twice() {
        let mut read_attempt = 0;

        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| {
                read_attempt += 1;
                match read_attempt {
                    1 => Ok(b"State one".to_vec()),
                    _ => panic!("Third read() call."),
                }
            }),
            async move |proxy| {
                assert_read!(proxy, "State one");
                assert_seek!(proxy, 0, Start);
                assert_read!(proxy, "State one");
                assert_close!(proxy);
            },
        );

        assert_eq!(read_attempt, 1);
    }

    #[test]
    fn write_twice() {
        let mut write_attempt = 0;

        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                write_attempt += 1;
                match write_attempt {
                    1 => {
                        assert_eq!(&*content, b"Write one and two");
                        Ok(())
                    }
                    _ => panic!("Second write() call.  Content: '{:?}'", content),
                }
            }),
            async move |proxy| {
                assert_write!(proxy, "Write one");
                assert_write!(proxy, " and two");
                assert_close!(proxy);
            },
        );

        assert_eq!(write_attempt, 1);
    }

    #[test]
    fn read_error() {
        let mut read_attempt = 0;

        let flags = OPEN_RIGHT_READABLE;
        let mut server = read_only(|| {
            read_attempt += 1;
            match read_attempt {
                1 => Err(Status::SHOULD_WAIT),
                2 => Ok(b"Have value".to_vec()),
                _ => panic!("Third call to read()."),
            }
        });

        {
            // Need an executor to create connection endpoints.
            let _exec = fasync::Executor::new().expect("Executor creation failed");

            let (_, server_end) =
                create_endpoints::<FileMarker>().expect("Failed to create connection endpoints");

            let first_connection_error = server
                .add_request_stream(flags, server_end.into_stream().unwrap())
                .expect_err("add_request_stream() should fail for the first time");
            assert_eq!(first_connection_error, Status::SHOULD_WAIT);
        }

        run_server_client(flags, server, async move |proxy| {
            assert_read!(proxy, "Have value");
            assert_close!(proxy);
        });
    }

    #[test]
    fn read_write_no_write_flag() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_write(
                || Ok(b"Can read".to_vec()),
                100,
                |_content| {
                    panic!("File was not opened as writable");
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Can read");
                assert_write_err!(proxy, "Can write", Status::ACCESS_DENIED);
                assert_write_at_err!(proxy, 0, "Can write", Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_write_no_read_flag() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            read_write(
                || {
                    panic!("File was not opened as readable");
                },
                100,
                |content| {
                    assert_eq!(*&content, b"Can write");
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read_err!(proxy, Status::ACCESS_DENIED);
                assert_read_at_err!(proxy, 0, Status::ACCESS_DENIED);
                assert_write!(proxy, "Can write");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    /// When the read handler returns a value that is larger then the specified capacity of the
    /// file, write handler will receive it as is, uncut.  This behaviour is specified in the
    /// description of [`PseudoFileImpl::capacity`].
    fn read_returns_more_than_capacity() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Read handler may return more than capacity".to_vec()),
                10,
                |content| {
                    assert_eq!(
                        content,
                        b"Write then could write beyond max capacity".to_vec()
                    );
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Read");
                assert_seek!(proxy, 0, Start);
                // Need to write something, otherwise write handler will not be called.
                // " capacity" is a leftover from what read handler has returned.
                assert_write!(proxy, "Write then could write beyond max");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_error() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"Wrong format");
                Err(Status::INVALID_ARGS)
            }),
            async move |proxy| {
                assert_write!(proxy, "Wrong");
                assert_write!(proxy, " format");
                assert_close_err!(proxy, Status::INVALID_ARGS);
            },
        );
    }

    #[test]
    fn open_truncate() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE,
            read_write(
                || panic!("OPEN_FLAG_TRUNCATE means read() is not called."),
                100,
                |content| {
                    assert_eq!(*&content, b"File content");
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_write!(proxy, "File content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_at_0() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| Ok(b"Whole file content".to_vec())),
            async move |proxy| {
                assert_read_at!(proxy, 0, "Whole file content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_at_overlapping() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| Ok(b"Content of the file".to_vec())),
            //                0         1
            //                0123456789012345678
            async move |proxy| {
                assert_read_at!(proxy, 3, "tent of the");
                assert_read_at!(proxy, 11, "the file");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn read_mixed_with_read_at() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| Ok(b"Content of the file".to_vec())),
            //                0         1
            //                0123456789012345678
            async move |proxy| {
                assert_read!(proxy, "Content");
                assert_read_at!(proxy, 3, "tent of the");
                assert_read!(proxy, " of the ");
                assert_read_at!(proxy, 11, "the file");
                assert_read!(proxy, "file");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_at_0() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"File content");
                Ok(())
            }),
            async move |proxy| {
                assert_write_at!(proxy, 0, "File content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_at_overlapping() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"Whole file content");
                //                      0         1
                //                      012345678901234567
                Ok(())
            }),
            async move |proxy| {
                assert_write_at!(proxy, 8, "le content");
                assert_write_at!(proxy, 6, "file");
                assert_write_at!(proxy, 0, "Whole file");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_mixed_with_write_at() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"Whole file content");
                //                      0         1
                //                      012345678901234567
                Ok(())
            }),
            async move |proxy| {
                assert_write!(proxy, "whole");
                assert_write_at!(proxy, 0, "Who");
                assert_write!(proxy, " 1234 ");
                assert_write_at!(proxy, 6, "file");
                assert_write!(proxy, "content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_read_write() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Initial".to_vec()),
                100,
                |content| {
                    assert_eq!(*&content, b"Final content");
                    //                    0         1
                    //                    0123456789012
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Init");
                assert_write!(proxy, "l con");
                // buffer: "Initl con"
                assert_seek!(proxy, 0, Start);
                assert_write!(proxy, "Fina");
                // buffer: "Final con"
                assert_seek!(proxy, 0, End, 9);
                assert_write!(proxy, "tent");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_valid_positions() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| Ok(b"Long file content".to_vec())),
            //                    0         1
            //                    01234567890123456
            async move |proxy| {
                assert_seek!(proxy, 5, Start);
                assert_read!(proxy, "file");
                assert_seek!(proxy, 1, Current, 10);
                assert_read!(proxy, "content");
                assert_seek!(proxy, -12, End, 5);
                assert_read!(proxy, "file content");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_valid_after_size_before_capacity() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content".to_vec()),
                //      0123456
                100,
                |content| {
                    assert_eq!(*&content, b"Content extended further");
                    //                      0         1         2
                    //                      012345678901234567890123
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_seek!(proxy, 7, Start);
                assert_read_err!(proxy, Status::OUT_OF_RANGE);
                assert_write!(proxy, " ext");
                //      "Content ext"));
                assert_seek!(proxy, 3, Current, 14);
                assert_write!(proxy, "ed");
                //      "Content ext000ed"));
                assert_seek!(proxy, 4, End, 20);
                assert_write!(proxy, "ther");
                //      "Content ext000ed0000ther"));
                //       0         1         2
                //       012345678901234567890123
                assert_seek!(proxy, 11, Start);
                assert_write!(proxy, "end");
                assert_seek!(proxy, 16, Start);
                assert_write!(proxy, " fur");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_invalid_before_0() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| Ok(b"Seek position is unaffected".to_vec())),
            //                    0         1         2
            //                    012345678901234567890123456
            async move |proxy| {
                assert_seek_err!(proxy, -10, Current, Status::OUT_OF_RANGE, 0);
                assert_read!(proxy, "Seek");
                assert_seek_err!(proxy, -10, Current, Status::OUT_OF_RANGE, 4);
                assert_read!(proxy, " position");
                assert_seek_err!(proxy, -100, End, Status::OUT_OF_RANGE, 13);
                assert_read!(proxy, " is unaffected");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_invalid_after_capacity() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content".to_vec()),
                //      0123456
                10,
                |_content| panic!("No writes should have happened"),
            ),
            async move |proxy| {
                assert_seek!(proxy, 8, Start);
                assert_seek_err!(proxy, 12, Start, Status::OUT_OF_RANGE, 8);
                assert_seek_err!(proxy, 3, Current, Status::OUT_OF_RANGE, 8);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn seek_after_truncate() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content".to_vec()),
                //      0123456
                100,
                |content| {
                    assert_eq!(*&content, b"Content\0\0\0end");
                    //                      0            1
                    //                      01234567 8 9 012
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_truncate!(proxy, 12);
                assert_seek!(proxy, 10, Start);
                assert_write!(proxy, "end");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    /// Make sure that even if the file content is larger than the capacity, seek does not allow to
    /// go beyond the maximum of the capacity and length.
    fn seek_beyond_capacity_in_large_file() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Long content".to_vec()),
                //      0         1
                //      012345678901
                8,
                |_content| panic!("No writes should have happened"),
            ),
            async move |proxy| {
                assert_seek!(proxy, 10, Start);
                assert_seek_err!(proxy, 12, Start, Status::OUT_OF_RANGE, 10);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn truncate_to_0() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content".to_vec()),
                //      0123456
                100,
                |content| {
                    assert_eq!(*&content, b"Replaced");
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Content");
                assert_truncate!(proxy, 0);
                // truncate should not change the seek position.
                assert_seek!(proxy, 0, Current, 7);
                assert_seek!(proxy, 0, Start);
                assert_write!(proxy, "Replaced");
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn write_then_truncate() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(100, |content| {
                assert_eq!(*&content, b"Replaced");
                Ok(())
            }),
            async move |proxy| {
                assert_write!(proxy, "Replaced content");
                assert_truncate!(proxy, 8);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn truncate_beyond_capacity() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(10, |_content| panic!("No writes should have happened")),
            async move |proxy| {
                assert_truncate_err!(proxy, 20, Status::OUT_OF_RANGE);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn truncate_read_only_file() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| Ok(b"Read-only content".to_vec())),
            async move |proxy| {
                assert_truncate_err!(proxy, 10, Status::ACCESS_DENIED);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    /// Make sure that when the read hander has returned a buffer that is larger than the capacity,
    /// we can cut it down to a something that is still larger then the capacity.  But we can not
    /// undo that cut.
    fn truncate_large_file_beyond_capacity() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content is very long".to_vec()),
                //      0         1
                //      01234567890123456789
                10,
                |content| {
                    assert_eq!(*&content, b"Content is very");
                    //                      0         1
                    //                      012345678901234
                    Ok(())
                },
            ),
            async move |proxy| {
                assert_read!(proxy, "Content");
                assert_truncate_err!(proxy, 40, Status::OUT_OF_RANGE);
                assert_truncate!(proxy, 16);
                assert_truncate!(proxy, 14);
                assert_truncate_err!(proxy, 16, Status::OUT_OF_RANGE);
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn clone_reduce_access() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Initial content".to_vec()),
                100,
                |content| {
                    assert_eq!(*&content, b"As updated");
                    Ok(())
                },
            ),
            async move |first_proxy| {
                assert_read!(first_proxy, "Initial content");
                assert_truncate!(first_proxy, 0);
                assert_seek!(first_proxy, 0, Start);
                assert_write!(first_proxy, "As updated");

                let (second_client_end, second_server_end) =
                    create_endpoints::<FileMarker>().unwrap();

                // TODO second_server_end is ServerEnd<FileMarker> but we need
                // ServerEnd<NodeMarker> :(
                first_proxy
                    .clone(
                        OPEN_RIGHT_READABLE | OPEN_FLAG_STATUS,
                        ServerEnd::new(second_server_end.into_channel()),
                    )
                    .unwrap();

                let second_proxy = second_client_end.into_proxy().unwrap();

                let event_stream = second_proxy.take_event_stream();
                match await!(event_stream.into_future()) {
                    (Some(Ok(FileEvent::OnOpen_ { s, info })), _) => {
                        assert_eq!(s, ZX_OK);
                        assert_eq!(info, None);
                    }
                    (unexpected, _) => {
                        panic!("Unexpected event: {:?}", unexpected);
                    }
                }

                assert_read!(second_proxy, "Initial content");
                assert_truncate_err!(second_proxy, 0, Status::ACCESS_DENIED);
                assert_write_err!(second_proxy, "As updated", Status::ACCESS_DENIED);

                assert_close!(first_proxy);
                assert_close!(second_proxy);
            },
        );
    }

    #[test]
    fn get_attr_read_only() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only(|| Ok(b"Content".to_vec())),
            async move |proxy| {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE | S_IRUSR,
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_attr_write_only() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only(10, |_content| panic!("No changes")),
            async move |proxy| {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE | S_IWUSR,
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_attr_read_write() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write(
                || Ok(b"Content".to_vec()),
                10,
                |_content| panic!("No changes"),
            ),
            async move |proxy| {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE | S_IWUSR | S_IRUSR,
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_attr_read_only_attr() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_only_attr(
                || Ok(b"Content".to_vec()),
                S_IXOTH | S_IROTH | S_IXGRP | S_IRGRP | S_IXUSR | S_IRUSR,
            ),
            async move |proxy| {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE
                            | (S_IXOTH | S_IROTH | S_IXGRP | S_IRGRP | S_IXUSR | S_IRUSR),
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_attr_write_only_attr() {
        run_server_client(
            OPEN_RIGHT_WRITABLE,
            write_only_attr(
                10,
                |_content| panic!("No changes"),
                S_IWOTH | S_IWGRP | S_IWUSR,
            ),
            async move |proxy| {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE | S_IWOTH | S_IWGRP | S_IWUSR,
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn get_attr_read_write_attr() {
        run_server_client(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            read_write_attr(
                || Ok(b"Content".to_vec()),
                10,
                |_content| panic!("No changes"),
                S_IXOTH
                    | S_IROTH
                    | S_IWOTH
                    | S_IXGRP
                    | S_IRGRP
                    | S_IWGRP
                    | S_IXUSR
                    | S_IRUSR
                    | S_IWUSR,
            ),
            async move |proxy| {
                assert_get_attr!(
                    proxy,
                    NodeAttributes {
                        mode: MODE_TYPE_FILE
                            | (S_IXOTH
                                | S_IROTH
                                | S_IWOTH
                                | S_IXGRP
                                | S_IRGRP
                                | S_IWGRP
                                | S_IXUSR
                                | S_IRUSR
                                | S_IWUSR),
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 1,
                        creation_time: 0,
                        modification_time: 0,
                    }
                );
                assert_close!(proxy);
            },
        );
    }

    #[test]
    fn clone_cannot_increase_access() {
        run_server_client(
            OPEN_RIGHT_READABLE,
            read_write(
                || Ok(b"Initial content".to_vec()),
                100,
                |_content| {
                    panic!("Clone should not be able to write.");
                },
            ),
            async move |first_proxy| {
                assert_read!(first_proxy, "Initial content");
                assert_write_err!(first_proxy, "Write attempt", Status::ACCESS_DENIED);

                let (second_client_end, second_server_end) =
                    create_endpoints::<FileMarker>().unwrap();

                // TODO second_server_end is ServerEnd<FileMarker> but we need
                // ServerEnd<NodeMarker> :(  Should we add an implementation of From<> to the FIDL
                // compiler output?
                first_proxy
                    .clone(
                        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_STATUS,
                        ServerEnd::new(second_server_end.into_channel()),
                    )
                    .unwrap();

                let second_proxy = second_client_end.into_proxy().unwrap();

                let event_stream = second_proxy.take_event_stream();
                match await!(event_stream.into_future()) {
                    (Some(Ok(FileEvent::OnOpen_ { s, info })), _) => {
                        assert_eq!(s, ZX_ERR_ACCESS_DENIED);
                        assert_eq!(info, None);
                    }
                    (unexpected, _) => {
                        panic!("Unexpected event: {:?}", unexpected);
                    }
                }

                assert_read_fidl_err!(second_proxy, fidl::Error::ClientWrite(Status::PEER_CLOSED));
                assert_write_fidl_err!(
                    second_proxy,
                    "Write attempt",
                    fidl::Error::ClientWrite(Status::PEER_CLOSED)
                );

                assert_close!(first_proxy);
            },
        );
    }

    /// A helper to create a "mock" directory holding the specified pseudo file.  The only
    /// interface the "directory" provides is the ability to open new connections to the pseudo
    /// file.  Otherwise the directory is running the file and the "new connections" stream.
    fn mock_single_file_directory(
        flags: u32, mut file: impl PseudoFile,
    ) -> (
        mpsc::Sender<ServerEnd<FileMarker>>,
        impl Future<Output = ()>,
    ) {
        let (open_requests_tx, open_requests_rx) = mpsc::channel::<ServerEnd<FileMarker>>(0);
        // Remember whether or not the `open_requests_rx` stream was closed between loop
        // iterations.
        let mut open_requests_rx = open_requests_rx.fuse();
        (
            open_requests_tx,
            async move {
                loop {
                    select! {
                        _ = file => panic!("file should never complete"),
                        open_req = open_requests_rx.next() => {
                            // open_requests stream should not be closed while the directory is
                            // still running.  It will be closed by the destructor, but at that
                            // time the directory should not be running any more.
                            let server_end = open_req.expect(
                                "open_requests stream closed while the directory still running");
                            file
                                .add_request_stream(flags, server_end.into_stream().unwrap())
                                .expect("add_request_stream() failed");
                        },
                    }
                }
            },
        )
    }

    #[test]
    fn mock_directory_with_one_file_and_two_connections() {
        let mut exec = fasync::Executor::new().expect("Executor creation failed");

        let read_count = RefCell::new(0);

        let (open_requests_sender, directory) = mock_single_file_directory(
            OPEN_RIGHT_READABLE,
            read_only_str(|| {
                let mut count = read_count.borrow_mut();
                *count += 1;
                Ok(format!("Content {}", *count))
            }),
        );

        // If futures::join would provide a way to "unpack" an incomplete (or complete) Join
        // future, this test could have been written a bit easier.  Without the unpack
        // functionality as soon as the pseudo_file is combined the client into a joined future,
        // there is no way to add new connections to it.  So I need to have a select! loop.  I do
        // not see a reason why Join should not allow to unpack, except that this functionality is
        // probably only useful in tests or some other very special situations, so it was probably
        // just not implemented yet.
        //
        // On the up side, this test implementation is half way of what a pseudo directory will
        // (maybe already does) look like - may catch more relevant bugs.
        //
        // I was thinking about something like this:
        //
        // let future = server.join(first_client);
        // pin_mut!(future);
        // if let Poll::Ready((Err(e), ())) = exec.run_until_stalled(&mut future) {
        //     panic!("Server failed: {:?}", e);
        // }
        //
        // let (server, first_client) = match future.unpack() {
        //     Join(MaybeDone::Future(server), MaybeDone::Future(first_client))
        //         => (server, first_client),
        //     Join(MaybeDone::Done(res), _) => panic!("Server has stopped: {:?}", res),
        //     Join(_, MaybeDone::Done(())) => panic!("First client has stopped"),
        //     _ => panic!("Unepxected join state"),
        // };
        //
        // let (second_client, second_client_stream) = ...
        //
        // server.add_request_stream(flags, second_client_stream).unwrap();
        //
        // let future = server.join3(first_client);
        // pin_mut!(future);
        // if let Poll::Ready((Err(e), (), ())) = exec.run_until_stalled(&mut future) {
        //     panic!("Server failed: {:?}", e);
        // }

        let create_client =
            move |expected_content: &'static str,
                  mut open_requests_sender: mpsc::Sender<ServerEnd<FileMarker>>| {
                let (client_end, server_end) = create_endpoints::<FileMarker>()
                    .expect("Failed to create connection endpoints");

                let proxy = client_end.into_proxy().unwrap();

                let (start_sender, start_receiver) = oneshot::channel::<()>();
                let (read_and_close_sender, read_and_close_receiver) = oneshot::channel::<()>();

                (
                    async move {
                        await!(start_receiver);

                        await!(open_requests_sender.send(server_end)).unwrap();

                        assert_read!(proxy, expected_content);

                        await!(read_and_close_receiver);

                        assert_seek!(proxy, 0, Start);
                        assert_read!(proxy, expected_content);
                        assert_close!(proxy);
                    },
                    || {
                        start_sender.send(()).unwrap();
                    },
                    || {
                        read_and_close_sender.send(()).unwrap();
                    },
                )
            };

        let (client1, client1_start, client1_read_and_close) =
            create_client("Content 1", open_requests_sender.clone());

        let (client2, client2_start, client2_read_and_close) =
            create_client("Content 2", open_requests_sender.clone());

        let future = directory.join3(client1, client2);
        pin_mut!(future);

        let mut run_and_check_read_count = |expected_count| {
            if let Poll::Ready(((), (), ())) = exec.run_until_stalled(&mut future) {
                panic!("Future should not complete");
            }
            assert_eq!(*read_count.borrow(), expected_count);
        };

        run_and_check_read_count(0);

        client1_start();

        run_and_check_read_count(1);

        client2_start();

        run_and_check_read_count(2);

        client1_read_and_close();

        run_and_check_read_count(2);

        client2_read_and_close();

        run_and_check_read_count(2)
    }
}
