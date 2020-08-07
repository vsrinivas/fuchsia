// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{inherit_rights_for_clone, send_on_open_with_error, GET_FLAGS_VISIBLE},
    execution_scope::ExecutionScope,
    file::{
        common::{
            new_connection_validate_flags, POSIX_READ_ONLY_PROTECTION_ATTRIBUTES,
            POSIX_READ_WRITE_PROTECTION_ATTRIBUTES, POSIX_WRITE_ONLY_PROTECTION_ATTRIBUTES,
        },
        File, SharingMode,
    },
};

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        FileMarker, FileObject, FileRequest, FileRequestStream, NodeAttributes, NodeInfo,
        NodeMarker, SeekOrigin, INO_UNKNOWN, MODE_TYPE_FILE, OPEN_FLAG_APPEND, OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        VMO_FLAG_EXACT, VMO_FLAG_EXEC, VMO_FLAG_PRIVATE, VMO_FLAG_READ, VMO_FLAG_WRITE,
    },
    fidl_fuchsia_mem::Buffer,
    fuchsia_zircon::{sys::ZX_OK, Status},
    futures::stream::StreamExt,
    static_assertions::assert_eq_size,
    std::sync::Arc,
};

/// Represents a FIDL connection to a file.
pub struct FileConnection<T: File> {
    /// Execution scope this connection and any async operations and connections it creates will
    /// use.
    scope: ExecutionScope,

    /// File this connection is associated with.
    file: Arc<T>,

    /// Wraps a FIDL connection, providing messages coming from the client.
    requests: FileRequestStream,

    /// Either the "flags" value passed into [`DirectoryEntry::open()`], or the "flags" value
    /// received with [`FileRequest::Clone()`].
    flags: u32,

    /// Flag passed into `create_connection`, that is used to limit read operations on this
    /// connection.
    readable: bool,

    /// Flag passed into `create_connection`, that is used to limit write operations on this
    /// connection.
    writable: bool,

    /// Seek position. Next byte to be read or written within the buffer. This might be beyond the
    /// current size of buffer, matching POSIX:
    ///
    ///     http://pubs.opengroup.org/onlinepubs/9699919799/functions/lseek.html
    ///
    /// It will cause the buffer to be extended with zeroes (if necessary) when write() is called.
    // While the content in the buffer vector uses usize for the size, it is easier to use u64 to
    // match the FIDL bindings API. Pseudo files are not expected to cross the 2^64 bytes size
    // limit. And all the code is much simpler when we just assume that usize is the same as u64.
    // Should we need to port to a 128 bit platform, there are static assertions in the code that
    // would fail.
    seek: u64,
}

/// Return type for [`handle_request()`] functions.
enum ConnectionState {
    /// Connection is still alive.
    Alive,
    /// Connection have received Node::Close message and the [`handle_close`] method has been
    /// already called for this connection.
    Closed,
    /// Connection has been dropped by the peer or an error has occurred.  [`handle_close`] still
    /// need to be called (though it would not be able to report the status to the peer).
    Dropped,
}

impl<T: 'static + File> FileConnection<T> {
    /// Initialized a file connection, which will be running in the context of the specified
    /// execution `scope`.  This function will also check the flags and will send the `OnOpen`
    /// event if necessary.
    ///
    /// Per connection buffer is initialized using the `init_buffer` closure, as part of the
    /// connection initialization.
    pub fn create_connection(
        scope: ExecutionScope,
        file: Arc<T>,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
        readable: bool,
        writable: bool,
    ) {
        let task = Self::create_connection_task(
            scope.clone(),
            file,
            flags,
            mode,
            server_end,
            readable,
            writable,
        );
        // If we failed to send the task to the executor, it is probably shut down or is in the
        // process of shutting down (this is the only error state currently).  So there is nothing
        // for us to do, but to ignore the open.  `server_end` will be closed when the object will
        // be dropped - there seems to be no error to report there.
        let _ = scope.spawn(Box::pin(task));
    }

    async fn create_connection_task(
        scope: ExecutionScope,
        file: Arc<T>,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
        readable: bool,
        writable: bool,
    ) {
        let flags = match new_connection_validate_flags(
            flags, mode, readable, writable, /*append_allowed=*/ true,
        ) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        match file.open(flags).await {
            Ok(()) => (),
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        if flags & OPEN_FLAG_TRUNCATE != 0 {
            if let Err(status) = file.truncate(0).await {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        }

        let (requests, control_handle) =
            match ServerEnd::<FileMarker>::new(server_end.into_channel())
                .into_stream_and_control_handle()
            {
                Ok((requests, control_handle)) => (requests, control_handle),
                Err(_) => {
                    // As we report all errors on `server_end`, if we failed to send an error over
                    // this connection, there is nowhere to send the error to.
                    return;
                }
            };

        if flags & OPEN_FLAG_DESCRIBE != 0 {
            let mut info = NodeInfo::File(FileObject { event: None, stream: None });
            match control_handle.send_on_open_(Status::OK.into_raw(), Some(&mut info)) {
                Ok(()) => (),
                Err(_) => return,
            }
        }

        let handle_requests = FileConnection {
            scope: scope.clone(),
            file,
            requests,
            flags,
            readable,
            writable,
            seek: 0,
        }
        .handle_requests();
        handle_requests.await;
    }

    async fn handle_requests(mut self) {
        while let Some(request_or_err) = self.requests.next().await {
            let state = match request_or_err {
                Err(_) => {
                    // FIDL level error, such as invalid message format and alike.  Close the
                    // connection on any unexpected error.
                    // TODO: Send an epitaph.
                    ConnectionState::Dropped
                }
                Ok(request) => {
                    self.handle_request(request)
                        .await
                        // Protocol level error.  Close the connection on any unexpected error.
                        // TODO: Send an epitaph.
                        .unwrap_or(ConnectionState::Dropped)
                }
            };

            match state {
                ConnectionState::Alive => (),
                ConnectionState::Closed => {
                    // We have already called `handle_close`, do not call it again.
                    return;
                }
                ConnectionState::Dropped => break,
            }
        }

        // If the connection has been closed by the peer or due some error we still need to call
        // the `updated` callback, unless the `Close` message have been used.
        // `ConnectionState::Closed` is handled above.
        let _ = self.file.close().await;
    }

    /// POSIX protection attributes are hard coded, as we are expecting them to be removed from the
    /// io.fidl altogether.
    fn posix_protection_attributes(&self) -> u32 {
        match (self.readable, self.writable) {
            (true, true) => POSIX_READ_WRITE_PROTECTION_ATTRIBUTES,
            (true, false) => POSIX_READ_ONLY_PROTECTION_ATTRIBUTES,
            (false, true) => POSIX_WRITE_ONLY_PROTECTION_ATTRIBUTES,
            (false, false) => 0,
        }
    }

    /// Handle a [`FileRequest`]. This function is responsible for handing all the file operations
    /// that operate on the connection-specific buffer.
    async fn handle_request(&mut self, req: FileRequest) -> Result<ConnectionState, Error> {
        match req {
            FileRequest::Clone { flags, object, control_handle: _ } => {
                self.handle_clone(self.flags, flags, object);
            }
            FileRequest::Close { responder } => {
                let status = self.file.close().await.err().unwrap_or(Status::OK);
                // We are going to close the connection anyways, so there is no way to handle this
                // error.  TODO We may want to send it in an epitaph.
                let _ = responder.send(status.into_raw());
                return Ok(ConnectionState::Closed);
            }
            FileRequest::Describe { responder } => {
                let mut info = NodeInfo::File(FileObject { event: None, stream: None });
                responder.send(&mut info)?;
            }
            FileRequest::Sync { responder } => {
                let status = self.file.sync().await.err().unwrap_or(Status::OK);
                responder.send(status.into_raw())?;
            }
            FileRequest::GetAttr { responder } => {
                let (status, mut attrs) = self.handle_get_attr().await;
                responder.send(status.into_raw(), &mut attrs)?;
            }
            FileRequest::SetAttr { flags, attributes, responder } => {
                let status = self.handle_set_attr(flags, attributes).await;
                responder.send(status.into_raw())?;
            }
            FileRequest::Read { count, responder } => {
                let (status, content) = self.handle_read(count).await;
                responder.send(status.into_raw(), &content)?;
            }
            FileRequest::ReadAt { offset, count, responder } => {
                let (status, content) = self.handle_read_at(offset, count).await;
                responder.send(status.into_raw(), &content)?;
            }
            FileRequest::Write { data, responder } => {
                let (status, actual) = self.handle_write(&data).await;
                responder.send(status.into_raw(), actual)?;
            }
            FileRequest::WriteAt { offset, data, responder } => {
                let (status, actual) = self.handle_write_at(offset, &data).await;
                responder.send(status.into_raw(), actual)?;
            }
            FileRequest::Seek { offset, start, responder } => {
                let (status, seek) = self.handle_seek(offset, start).await;
                responder.send(status.into_raw(), seek)?;
            }
            FileRequest::Truncate { length, responder } => {
                let status = self.handle_truncate(length).await;
                responder.send(status.into_raw())?;
            }
            FileRequest::GetFlags { responder } => {
                responder.send(ZX_OK, self.flags & GET_FLAGS_VISIBLE)?;
            }
            FileRequest::NodeGetFlags { responder } => {
                responder.send(ZX_OK, self.flags & GET_FLAGS_VISIBLE)?;
            }
            FileRequest::SetFlags { flags, responder } => {
                self.flags = (self.flags & !OPEN_FLAG_APPEND) | (flags & OPEN_FLAG_APPEND);
                responder.send(ZX_OK)?;
            }
            FileRequest::NodeSetFlags { flags, responder } => {
                self.flags = (self.flags & !OPEN_FLAG_APPEND) | (flags & OPEN_FLAG_APPEND);
                responder.send(ZX_OK)?;
            }
            FileRequest::GetBuffer { flags, responder } => {
                let (status, mut buffer) = self.handle_get_buffer(flags).await;
                responder.send(status.into_raw(), buffer.as_mut())?;
            }
        }
        Ok(ConnectionState::Alive)
    }

    fn handle_clone(&mut self, parent_flags: u32, flags: u32, server_end: ServerEnd<NodeMarker>) {
        let flags = match inherit_rights_for_clone(parent_flags, flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        Self::create_connection(
            self.scope.clone(),
            self.file.clone(),
            flags,
            0,
            server_end,
            self.readable,
            self.writable,
        );
    }

    async fn handle_get_attr(&mut self) -> (Status, NodeAttributes) {
        let mut attributes = match self.file.get_attrs().await {
            Ok(attr) => attr,
            Err(status) => {
                return (
                    status,
                    NodeAttributes {
                        mode: 0,
                        id: INO_UNKNOWN,
                        content_size: 0,
                        storage_size: 0,
                        link_count: 0,
                        creation_time: 0,
                        modification_time: 0,
                    },
                )
            }
        };

        attributes.mode = MODE_TYPE_FILE | self.posix_protection_attributes();
        (Status::OK, attributes)
    }

    async fn handle_read(&mut self, count: u64) -> (Status, Vec<u8>) {
        let (status, content) = self.handle_read_at(self.seek, count).await;
        assert_eq_size!(usize, u64);
        self.seek += content.len() as u64;
        (status, content)
    }

    async fn handle_read_at(&mut self, offset: u64, count: u64) -> (Status, Vec<u8>) {
        if self.flags & OPEN_RIGHT_READABLE == 0 {
            return (Status::BAD_HANDLE, vec![]);
        }

        match self.file.read_at(offset, count).await {
            Ok(vec) => (Status::OK, vec),
            Err(e) => (e, vec![]),
        }
    }

    async fn handle_write(&mut self, content: &[u8]) -> (Status, u64) {
        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            return (Status::BAD_HANDLE, 0);
        }

        if self.flags & OPEN_FLAG_APPEND != 0 {
            match self.file.append(content).await {
                Ok((bytes, offset)) => {
                    self.seek = offset;
                    (Status::OK, bytes)
                }
                Err(e) => (e, 0),
            }
        } else {
            let (status, actual) = self.handle_write_at(self.seek, content).await;
            assert_eq_size!(usize, u64);
            self.seek += actual;
            (status, actual)
        }
    }

    async fn handle_write_at(&mut self, offset: u64, content: &[u8]) -> (Status, u64) {
        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            return (Status::BAD_HANDLE, 0);
        }

        match self.file.write_at(offset, content).await {
            Ok(bytes) => (Status::OK, bytes),
            Err(e) => (e, 0),
        }
    }

    /// Move seek position to byte `offset` relative to the origin specified by `start`.
    async fn handle_seek(&mut self, offset: i64, start: SeekOrigin) -> (Status, u64) {
        if self.flags & OPEN_FLAG_NODE_REFERENCE != 0 {
            return (Status::BAD_HANDLE, 0);
        }

        let (status, new_seek) = match start {
            SeekOrigin::Start => (Status::OK, offset as i128),

            SeekOrigin::Current => {
                assert_eq_size!(usize, i64);
                (Status::OK, self.seek as i128 + offset as i128)
            }

            SeekOrigin::End => {
                let size = self.file.get_size().await;
                assert_eq_size!(usize, i64, u64);
                match size {
                    Ok(size) => (Status::OK, size as i128 + offset as i128),
                    Err(e) => (e, self.seek as i128),
                }
            }
        };

        if new_seek < 0 {
            // Can't seek to before the end of a file.
            return (Status::OUT_OF_RANGE, self.seek);
        } else {
            self.seek = new_seek as u64;
            return (status, self.seek);
        }
    }

    async fn handle_set_attr(&mut self, flags: u32, attrs: NodeAttributes) -> Status {
        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            return Status::BAD_HANDLE;
        }

        match self.file.set_attrs(flags, attrs).await {
            Ok(()) => Status::OK,
            Err(status) => status,
        }
    }

    async fn handle_truncate(&mut self, length: u64) -> Status {
        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            return Status::BAD_HANDLE;
        }

        match self.file.truncate(length).await {
            Ok(()) => Status::OK,
            Err(status) => status,
        }
    }

    async fn handle_get_buffer(&mut self, flags: u32) -> (Status, Option<Buffer>) {
        let mode = match Self::get_buffer_validate_flags(flags, self.flags) {
            Err(status) => return (status, None),
            Ok(mode) => mode,
        };

        match self.file.get_buffer(mode, flags).await {
            Ok(buf) => (Status::OK, buf),
            Err(e) => (e, None),
        }
    }

    fn get_buffer_validate_flags(
        new_vmo_flags: u32,
        connection_flags: u32,
    ) -> Result<SharingMode, Status> {
        if connection_flags & OPEN_RIGHT_READABLE == 0
            && (new_vmo_flags & VMO_FLAG_READ != 0 || new_vmo_flags & VMO_FLAG_EXEC != 0)
        {
            return Err(Status::ACCESS_DENIED);
        }

        if connection_flags & OPEN_RIGHT_WRITABLE == 0 && new_vmo_flags & VMO_FLAG_WRITE != 0 {
            return Err(Status::ACCESS_DENIED);
        }

        if new_vmo_flags & VMO_FLAG_PRIVATE != 0 && new_vmo_flags & VMO_FLAG_EXACT != 0 {
            return Err(Status::INVALID_ARGS);
        }

        // We do not share the VMO itself with a WRITE flag, as this would allow someone to change
        // the size "under our feel" and there seems to be now way to protect from it.
        if new_vmo_flags & VMO_FLAG_EXACT != 0 && new_vmo_flags & VMO_FLAG_WRITE != 0 {
            return Err(Status::NOT_SUPPORTED);
        }

        // We use shared mode by default, if the caller did not specify.  It should be more
        // lightweight, I assume?  Except when a writable share is necessary.  `VMO_FLAG_EXACT |
        // VMO_FLAG_WRITE` is prohibited above.
        if new_vmo_flags & VMO_FLAG_PRIVATE != 0 || new_vmo_flags & VMO_FLAG_WRITE != 0 {
            Ok(SharingMode::Private)
        } else {
            Ok(SharingMode::Shared)
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        async_trait::async_trait,
        fidl_fuchsia_io::{
            FileEvent, FileProxy, NodeInfo, CLONE_FLAG_SAME_RIGHTS,
            NODE_ATTRIBUTE_FLAG_CREATION_TIME, NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME,
        },
        fuchsia_async as fasync,
        fuchsia_zircon::Status,
        futures::{future::BoxFuture, prelude::*},
        std::sync::Mutex,
    };

    #[derive(Debug, PartialEq)]
    enum FileOperation {
        Init { flags: u32 },
        ReadAt { offset: u64, count: u64 },
        WriteAt { offset: u64, content: Vec<u8> },
        Append { content: Vec<u8> },
        Truncate { length: u64 },
        GetBuffer { mode: SharingMode, flags: u32 },
        GetSize,
        GetAttrs,
        SetAttrs { flags: u32, attrs: NodeAttributes },
        Close,
        Sync,
    }

    type MockCallbackType = Box<Fn(&FileOperation) -> Status + Sync + Send>;
    /// A fake file that just tracks what calls `FileConnection` makes on it.
    struct MockFile {
        /// The list of operations that have been called.
        operations: Mutex<Vec<FileOperation>>,
        /// Callback used to determine how to respond to given operation.
        callback: MockCallbackType,
        /// Only used for get_size/get_attributes
        file_size: u64,
    }

    const MOCK_FILE_SIZE: u64 = 256;
    const MOCK_FILE_ID: u64 = 10;
    const MOCK_FILE_LINKS: u64 = 2;
    const MOCK_FILE_CREATION_TIME: u64 = 10;
    const MOCK_FILE_MODIFICATION_TIME: u64 = 100;
    impl MockFile {
        pub fn new(callback: MockCallbackType) -> Arc<Self> {
            Arc::new(MockFile {
                operations: Mutex::new(Vec::new()),
                callback,
                file_size: MOCK_FILE_SIZE,
            })
        }

        fn handle_operation(&self, operation: FileOperation) -> Result<(), Status> {
            let result = (self.callback)(&operation);
            self.operations.lock().unwrap().push(operation);
            match result {
                Status::OK => Ok(()),
                err => Err(err),
            }
        }
    }

    #[async_trait]
    impl File for MockFile {
        async fn open(&self, flags: u32) -> Result<(), Status> {
            self.handle_operation(FileOperation::Init { flags })?;
            Ok(())
        }

        async fn read_at(&self, offset: u64, count: u64) -> Result<Vec<u8>, Status> {
            self.handle_operation(FileOperation::ReadAt { offset, count })?;

            // Return data as if we were a file with 0..255 repeated endlessly.
            Ok((offset..offset + count).map(|i| (i % 256) as u8).collect())
        }

        async fn write_at(&self, offset: u64, content: &[u8]) -> Result<u64, Status> {
            self.handle_operation(FileOperation::WriteAt { offset, content: content.to_vec() })?;

            Ok(content.len() as u64)
        }

        async fn append(&self, content: &[u8]) -> Result<(u64, u64), Status> {
            self.handle_operation(FileOperation::Append { content: content.to_vec() })?;

            Ok((content.len() as u64, self.file_size + content.len() as u64))
        }

        async fn truncate(&self, length: u64) -> Result<(), Status> {
            self.handle_operation(FileOperation::Truncate { length })
        }

        async fn get_buffer(
            &self,
            mode: SharingMode,
            flags: u32,
        ) -> Result<Option<Buffer>, Status> {
            self.handle_operation(FileOperation::GetBuffer { mode, flags })?;
            Ok(None)
        }

        async fn get_size(&self) -> Result<u64, Status> {
            self.handle_operation(FileOperation::GetSize)?;
            Ok(self.file_size)
        }

        async fn get_attrs(&self) -> Result<NodeAttributes, Status> {
            self.handle_operation(FileOperation::GetAttrs)?;
            Ok(NodeAttributes {
                mode: 0,
                id: MOCK_FILE_ID,
                content_size: self.file_size,
                storage_size: 2 * self.file_size,
                link_count: MOCK_FILE_LINKS,
                creation_time: MOCK_FILE_CREATION_TIME,
                modification_time: MOCK_FILE_MODIFICATION_TIME,
            })
        }

        async fn set_attrs(&self, flags: u32, attrs: NodeAttributes) -> Result<(), Status> {
            self.handle_operation(FileOperation::SetAttrs { flags, attrs })
        }

        async fn close(&self) -> Result<(), Status> {
            self.handle_operation(FileOperation::Close)?;
            Ok(())
        }

        async fn sync(&self) -> Result<(), Status> {
            self.handle_operation(FileOperation::Sync)
        }
    }

    /// Only the init operation will succeed, all others fail.
    fn only_allow_init(op: &FileOperation) -> Status {
        match op {
            FileOperation::Init { .. } => Status::OK,
            _ => Status::IO,
        }
    }

    /// All operations succeed.
    fn always_succeed_callback(_op: &FileOperation) -> Status {
        Status::OK
    }

    struct TestEnv {
        pub file: Arc<MockFile>,
        pub proxy: FileProxy,
    }

    fn init_mock_file(callback: MockCallbackType, flags: u32) -> TestEnv {
        let file = MockFile::new(callback);
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<FileMarker>().expect("Create proxy to succeed");

        let scope = ExecutionScope::from_executor(Box::new(fasync::EHandle::local()));
        FileConnection::create_connection(
            scope,
            file.clone(),
            flags,
            0,
            server_end.into_channel().into(),
            true,
            true,
        );

        TestEnv { file, proxy }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_flag_truncate() {
        let env = init_mock_file(
            Box::new(always_succeed_callback),
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE,
        );
        env.proxy.sync().await.unwrap(); // Do a dummy sync() to make sure that the open has finished.
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE },
                FileOperation::Truncate { length: 0 },
                FileOperation::Sync,
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_clone_same_rights() {
        let env = init_mock_file(
            Box::new(always_succeed_callback),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        );
        // Read from original proxy.
        let (status, _) = env.proxy.read(6).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        let (clone_proxy, remote) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        env.proxy.clone(CLONE_FLAG_SAME_RIGHTS, remote.into_channel().into()).unwrap();
        // Read from clone_proxy.
        let (status, _) = clone_proxy.read(5).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);

        // Read from original proxy.
        let (status, _) = env.proxy.read(5).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);

        let events = env.file.operations.lock().unwrap();
        // Each connection should have an independent seek.
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE },
                FileOperation::ReadAt { offset: 0, count: 6 },
                FileOperation::Init { flags: OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE },
                FileOperation::ReadAt { offset: 0, count: 5 },
                FileOperation::ReadAt { offset: 6, count: 5 },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_close_succeeds() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let status = env.proxy.close().await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);

        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![FileOperation::Init { flags: OPEN_RIGHT_READABLE }, FileOperation::Close {},]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_close_fails() {
        let env = init_mock_file(Box::new(only_allow_init), OPEN_RIGHT_READABLE);
        let status = env.proxy.close().await.unwrap();
        assert_eq!(Status::from_raw(status), Status::IO);

        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![FileOperation::Init { flags: OPEN_RIGHT_READABLE }, FileOperation::Close {},]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_describe() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let info = env.proxy.describe().await.unwrap();
        match info {
            NodeInfo::File { .. } => (),
            _ => panic!("Expected NodeInfo::File, got {:?}", info),
        };
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_getattr() {
        let env = init_mock_file(Box::new(always_succeed_callback), 0);
        let (status, attributes) = env.proxy.get_attr().await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(
            attributes,
            NodeAttributes {
                mode: MODE_TYPE_FILE | POSIX_READ_WRITE_PROTECTION_ATTRIBUTES,
                id: MOCK_FILE_ID,
                content_size: MOCK_FILE_SIZE,
                storage_size: 2 * MOCK_FILE_SIZE,
                link_count: MOCK_FILE_LINKS,
                creation_time: MOCK_FILE_CREATION_TIME,
                modification_time: MOCK_FILE_MODIFICATION_TIME,
            }
        );
        let events = env.file.operations.lock().unwrap();
        assert_eq!(*events, vec![FileOperation::Init { flags: 0 }, FileOperation::GetAttrs,]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_getbuffer() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let (status, buffer) = env.proxy.get_buffer(VMO_FLAG_READ).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert!(buffer.is_none());
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_READABLE },
                FileOperation::GetBuffer { mode: SharingMode::Shared, flags: VMO_FLAG_READ },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_getbuffer_no_perms() {
        let env = init_mock_file(Box::new(always_succeed_callback), 0);
        let (status, buffer) = env.proxy.get_buffer(VMO_FLAG_READ).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::ACCESS_DENIED);
        assert!(buffer.is_none());
        let events = env.file.operations.lock().unwrap();
        assert_eq!(*events, vec![FileOperation::Init { flags: 0 },]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_getflags() {
        let env = init_mock_file(
            Box::new(always_succeed_callback),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE,
        );
        let (status, flags) = env.proxy.get_flags().await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        // OPEN_FLAG_TRUNCATE should get stripped because it only applies at open time.
        assert_eq!(flags, OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init {
                    flags: OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE
                },
                FileOperation::Truncate { length: 0 }
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_flag_describe() {
        let env = init_mock_file(
            Box::new(always_succeed_callback),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE,
        );
        let event = env.proxy.take_event_stream().try_next().await.unwrap();
        match event {
            Some(FileEvent::OnOpen_ { s, info: Some(boxed) }) => {
                assert_eq!(Status::from_raw(s), Status::OK);
                assert_eq!(*boxed, NodeInfo::File(FileObject { event: None, stream: None }));
            }
            e => panic!("Expected OnOpen event with NodeInfo::File, got {:?}", e),
        }
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![FileOperation::Init {
                flags: OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE
            },]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_succeeds() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let (status, data) = env.proxy.read(10).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(data, vec![0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);

        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_READABLE },
                FileOperation::ReadAt { offset: 0, count: 10 },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_not_readable() {
        let env = init_mock_file(Box::new(only_allow_init), OPEN_RIGHT_WRITABLE);
        let (status, _data) = env.proxy.read(10).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::BAD_HANDLE);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_at_succeeds() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let (status, data) = env.proxy.read_at(5, 10).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(data, vec![10, 11, 12, 13, 14]);

        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_READABLE },
                FileOperation::ReadAt { offset: 10, count: 5 },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_start() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let (status, offset) = env.proxy.seek(10, SeekOrigin::Start).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(offset, 10);

        let (status, data) = env.proxy.read(1).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(data, vec![10]);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_READABLE },
                FileOperation::ReadAt { offset: 10, count: 1 },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_cur() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let (status, offset) = env.proxy.seek(10, SeekOrigin::Start).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(offset, 10);

        let (status, offset) = env.proxy.seek(-2, SeekOrigin::Current).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(offset, 8);

        let (status, data) = env.proxy.read(1).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(data, vec![8]);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_READABLE },
                FileOperation::ReadAt { offset: 8, count: 1 },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_before_start() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let (status, offset) = env.proxy.seek(-4, SeekOrigin::Current).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OUT_OF_RANGE);
        assert_eq!(offset, 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek_end() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let (status, offset) = env.proxy.seek(-4, SeekOrigin::End).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(offset, MOCK_FILE_SIZE - 4);

        let (status, data) = env.proxy.read(1).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(data, vec![((MOCK_FILE_SIZE - 4) % 255) as u8]);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_READABLE },
                FileOperation::GetSize, // for the seek
                FileOperation::ReadAt { offset: MOCK_FILE_SIZE - 4, count: 1 },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_attrs() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_WRITABLE);
        let mut set_attrs = NodeAttributes {
            mode: 0,
            id: 0,
            content_size: 0,
            storage_size: 0,
            link_count: 0,
            creation_time: 40000,
            modification_time: 100000,
        };
        let status = env
            .proxy
            .set_attr(
                NODE_ATTRIBUTE_FLAG_CREATION_TIME | NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME,
                &mut set_attrs,
            )
            .await
            .unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_WRITABLE },
                FileOperation::SetAttrs {
                    flags: NODE_ATTRIBUTE_FLAG_CREATION_TIME
                        | NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME,
                    attrs: set_attrs
                },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_flags() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_WRITABLE);
        let status = env.proxy.set_flags(OPEN_FLAG_APPEND).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        let (status, flags) = env.proxy.get_flags().await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(flags, OPEN_RIGHT_WRITABLE | OPEN_FLAG_APPEND);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sync() {
        let env = init_mock_file(Box::new(always_succeed_callback), 0);
        let status = env.proxy.sync().await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(*events, vec![FileOperation::Init { flags: 0 }, FileOperation::Sync,]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_truncate() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_WRITABLE);
        let status = env.proxy.truncate(10).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_WRITABLE },
                FileOperation::Truncate { length: 10 },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_truncate_no_perms() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let status = env.proxy.truncate(10).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::BAD_HANDLE);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(*events, vec![FileOperation::Init { flags: OPEN_RIGHT_READABLE },]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_WRITABLE);
        let data = "Hello, world!".as_bytes();
        let (status, count) = env.proxy.write(data).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(count, data.len() as u64);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_WRITABLE },
                FileOperation::WriteAt { offset: 0, content: data.to_vec() },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_no_perms() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_READABLE);
        let data = "Hello, world!".as_bytes();
        let (status, _count) = env.proxy.write(data).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::BAD_HANDLE);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(*events, vec![FileOperation::Init { flags: OPEN_RIGHT_READABLE },]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_at() {
        let env = init_mock_file(Box::new(always_succeed_callback), OPEN_RIGHT_WRITABLE);
        let data = "Hello, world!".as_bytes();
        let (status, count) = env.proxy.write_at(data, 10).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(count, data.len() as u64);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_WRITABLE },
                FileOperation::WriteAt { offset: 10, content: data.to_vec() },
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_append() {
        let env = init_mock_file(
            Box::new(always_succeed_callback),
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_APPEND,
        );
        let data = "Hello, world!".as_bytes();
        let (status, count) = env.proxy.write(data).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(count, data.len() as u64);
        let (status, offset) = env.proxy.seek(0, SeekOrigin::Current).await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        assert_eq!(offset, MOCK_FILE_SIZE + data.len() as u64);
        let events = env.file.operations.lock().unwrap();
        assert_eq!(
            *events,
            vec![
                FileOperation::Init { flags: OPEN_RIGHT_WRITABLE | OPEN_FLAG_APPEND },
                FileOperation::Append { content: data.to_vec() },
            ]
        );
    }
}
