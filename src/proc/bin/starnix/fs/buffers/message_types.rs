// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::byteorder::{ByteOrder, NativeEndian};
use zerocopy::{AsBytes, FromBytes};

use crate::fs::socket::{SocketAddress, SocketMessageFlags};
use crate::fs::*;
use crate::mm::MemoryAccessor;
use crate::task::{CurrentTask, Task};
use crate::types::*;

/// A `Message` represents a typed segment of bytes within a `MessageQueue`.
#[derive(Clone, Debug)]
#[cfg_attr(test, derive(PartialEq))]
pub struct Message {
    /// The data contained in the message.
    pub data: MessageData,

    /// The address from which the message was sent.
    pub address: Option<SocketAddress>,

    /// The ancillary data that is associated with this message.
    pub ancillary_data: Vec<AncillaryData>,
}

impl Message {
    /// Creates a a new message with the provided message and ancillary data.
    pub fn new(
        data: MessageData,
        address: Option<SocketAddress>,
        ancillary_data: Vec<AncillaryData>,
    ) -> Self {
        Message { data, address, ancillary_data }
    }

    /// Returns the length of the message in bytes.
    ///
    /// Note that ancillary data does not contribute to the length of the message.
    pub fn len(&self) -> usize {
        self.data.len()
    }
}

impl From<MessageData> for Message {
    fn from(data: MessageData) -> Self {
        Message { data, address: None, ancillary_data: Vec::new() }
    }
}

impl From<Vec<u8>> for Message {
    fn from(data: Vec<u8>) -> Self {
        Self { data: data.into(), address: None, ancillary_data: Vec::new() }
    }
}

pub struct ControlMsg {
    pub header: cmsghdr,
    pub data: Vec<u8>,
}

impl ControlMsg {
    pub fn new(cmsg_level: u32, cmsg_type: u32, data: Vec<u8>) -> ControlMsg {
        let cmsg_len = std::mem::size_of::<cmsghdr>() + data.len();
        let header = cmsghdr { cmsg_len, cmsg_level, cmsg_type };
        ControlMsg { header, data }
    }
}

/// `AncillaryData` converts a `cmsghdr` into a representation suitable for passing around
/// inside of starnix. In AF_UNIX/SCM_RIGHTS, for example, the file descrpitors will be turned
/// into `FileHandle`s that can be sent to other tasks.
///
/// An `AncillaryData` instance can be converted back into a `cmsghdr`. At that point the contained
/// objects will be converted back to what can be stored in a `cmsghdr`. File handles, for example,
/// will be added to the reading task's files and the associated file descriptors will be stored in
///  the `cmsghdr`.
#[derive(Clone, Debug)]
#[cfg_attr(test, derive(PartialEq))]
pub enum AncillaryData {
    Unix(UnixControlData),
}

impl AncillaryData {
    /// Creates a new `AncillaryData` instance representing the data in `message`.
    ///
    /// # Parameters
    /// - `current_task`: The current task. Used to interpret SCM_RIGHTS messages.
    /// - `message`: The message header to parse.
    pub fn from_cmsg(current_task: &CurrentTask, message: ControlMsg) -> Result<Self, Errno> {
        if message.header.cmsg_level != SOL_SOCKET {
            return error!(
                EINVAL,
                format!("invalid cmsg_level {:?}", { message.header.cmsg_level })
            );
        }

        if message.header.cmsg_type != SCM_RIGHTS && message.header.cmsg_type != SCM_CREDENTIALS {
            return error!(EINVAL);
        }

        Ok(AncillaryData::Unix(UnixControlData::new(current_task, message)?))
    }

    /// Returns a `ControlMsg` representation of this `AncillaryData`. This includes
    /// creating any objects (e.g., file descriptors) in `task`.
    pub fn into_controlmsg(
        self,
        current_task: &CurrentTask,
        flags: SocketMessageFlags,
    ) -> Result<ControlMsg, Errno> {
        match self {
            AncillaryData::Unix(control) => control.into_controlmsg(current_task, flags),
        }
    }

    /// Returns the total size of all data in this message.
    pub fn total_size(&self) -> usize {
        match self {
            AncillaryData::Unix(control) => control.total_size(),
        }
    }

    /// Returns the minimum size that can fit some amount of this message's data.
    pub fn minimum_size(&self) -> usize {
        match self {
            AncillaryData::Unix(control) => control.minimum_size(),
        }
    }

    /// Convert the message into bytes, truncating it if it exceeds the available space.
    pub fn into_bytes(
        self,
        current_task: &CurrentTask,
        flags: SocketMessageFlags,
        space_available: usize,
    ) -> Result<Vec<u8>, Errno> {
        let header_size = std::mem::size_of::<cmsghdr>();
        let minimum_data_size = self.minimum_size();

        if space_available < header_size + minimum_data_size {
            // If there is not enough space available to fit the header, return an empty vector
            // instead of a partial header.
            return Ok(vec![]);
        }

        let mut cmsg = self.into_controlmsg(current_task, flags)?;
        let cmsg_len = std::cmp::min(header_size + cmsg.data.len(), space_available);
        cmsg.header.cmsg_len = cmsg_len;

        let mut bytes = cmsg.header.as_bytes().to_owned();
        bytes.extend_from_slice(&cmsg.data[..cmsg_len - header_size]);
        Ok(bytes)
    }
}

/// A control message for a Unix domain socket.
#[derive(Clone, Debug)]
pub enum UnixControlData {
    /// "Send or receive a set of open file descriptors from another process. The data portion
    /// contains an integer array of the file descriptors."
    ///
    /// See https://man7.org/linux/man-pages/man7/unix.7.html.
    Rights(Vec<FileHandle>),

    /// "Send or receive UNIX credentials.  This can be used for authentication. The credentials are
    /// passed as a struct ucred ancillary message."
    ///
    /// See https://man7.org/linux/man-pages/man7/unix.7.html.
    Credentials(ucred),

    /// "Receive the SELinux security context (the security label) of the peer socket. The received
    /// ancillary data is a null-terminated string containing the security context."
    ///
    /// See https://man7.org/linux/man-pages/man7/unix.7.html.
    Security(FsString),
}

/// `UnixControlData` cannot derive `PartialEq` due to `Rights` containing file handles.
///
/// This implementation only compares the number of files, not the actual files. The equality
/// should only be used for testing.
#[cfg(test)]
impl PartialEq for UnixControlData {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (UnixControlData::Rights(self_files), UnixControlData::Rights(other_files)) => {
                self_files.len() == other_files.len()
            }
            (
                UnixControlData::Credentials(self_credentials),
                UnixControlData::Credentials(other_credentials),
            ) => self_credentials == other_credentials,
            (
                UnixControlData::Security(self_security),
                UnixControlData::Security(other_security),
            ) => self_security == other_security,
            _ => false,
        }
    }
}

impl UnixControlData {
    /// Creates a new `UnixControlData` instance for the provided `message_header`. This includes
    /// reading the associated data from the `task` (e.g., files from file descriptors).
    pub fn new(current_task: &CurrentTask, message: ControlMsg) -> Result<Self, Errno> {
        match message.header.cmsg_type {
            SCM_RIGHTS => {
                // Compute the number of file descriptors that fit in the provided bytes.
                let bytes_per_file_descriptor = std::mem::size_of::<FdNumber>();
                let num_file_descriptors = message.data.len() / bytes_per_file_descriptor;

                // Get the files associated with the provided file descriptors.
                let files = (0..num_file_descriptors * bytes_per_file_descriptor)
                    .step_by(bytes_per_file_descriptor)
                    .map(|index| NativeEndian::read_i32(&message.data[index..]))
                    .map(|fd| current_task.files.get(FdNumber::from_raw(fd)))
                    .collect::<Result<Vec<FileHandle>, Errno>>()?;

                Ok(UnixControlData::Rights(files))
            }
            SCM_CREDENTIALS => {
                if message.data.len() < std::mem::size_of::<ucred>() {
                    return error!(EINVAL);
                }

                let credentials = ucred::read_from(&message.data[..std::mem::size_of::<ucred>()])
                    .ok_or_else(|| errno!(EINVAL))?;
                Ok(UnixControlData::Credentials(credentials))
            }
            SCM_SECURITY => Ok(UnixControlData::Security(message.data)),
            _ => error!(EINVAL),
        }
    }

    /// Returns a `UnixControlData` message that can be used when passcred is enabled but no
    /// credentials were sent.
    pub fn unknown_creds() -> Self {
        const NOBODY: u32 = 65534;
        let credentials = ucred { pid: 0, uid: NOBODY, gid: NOBODY };
        UnixControlData::Credentials(credentials)
    }

    /// Constructs a ControlMsg for this control data, with a destination of `task`.
    ///
    /// The provided `task` is used to create any required file descriptors, etc.
    pub fn into_controlmsg(
        self,
        current_task: &CurrentTask,
        flags: SocketMessageFlags,
    ) -> Result<ControlMsg, Errno> {
        let (msg_type, data) = match self {
            UnixControlData::Rights(files) => {
                let flags = if flags.contains(SocketMessageFlags::CMSG_CLOEXEC) {
                    FdFlags::CLOEXEC
                } else {
                    FdFlags::empty()
                };
                let fds: Vec<FdNumber> = files
                    .iter()
                    .map(|file| current_task.files.add_with_flags(file.clone(), flags))
                    .collect::<Result<Vec<FdNumber>, Errno>>()?;
                (SCM_RIGHTS, fds.as_bytes().to_owned())
            }
            UnixControlData::Credentials(credentials) => {
                (SCM_CREDENTIALS, credentials.as_bytes().to_owned())
            }
            UnixControlData::Security(string) => (SCM_SECURITY, string.as_bytes().to_owned()),
        };

        Ok(ControlMsg::new(SOL_SOCKET, msg_type, data))
    }

    /// Returns the total size of all data in this message.
    pub fn total_size(&self) -> usize {
        match self {
            UnixControlData::Rights(files) => files.len() * std::mem::size_of::<FdNumber>(),
            UnixControlData::Credentials(_credentials) => std::mem::size_of::<ucred>(),
            UnixControlData::Security(string) => string.len(),
        }
    }

    /// Returns the minimum size that can fit some amount of this message's data. For example, the
    /// minimum size for an SCM_RIGHTS message is the size of a single FD. If the buffer is large
    /// enough for the minimum size but too small for the total size, the message is truncated and
    /// the MSG_CTRUNC flag is set.
    pub fn minimum_size(&self) -> usize {
        match self {
            UnixControlData::Rights(_files) => std::mem::size_of::<FdNumber>(),
            UnixControlData::Credentials(_credentials) => 0,
            UnixControlData::Security(string) => string.len(),
        }
    }
}

/// A `Packet` stores an arbitrary sequence of bytes.
#[derive(Clone, Eq, PartialEq, Debug, Default)]
pub struct MessageData {
    /// The bytes in this packet.
    bytes: Vec<u8>,
}

impl MessageData {
    /// Copies data from user memory into a new MessageData object.
    pub fn copy_from_user(
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
        limit: usize,
    ) -> Result<MessageData, Errno> {
        let mut bytes = vec![0u8; limit];
        let mut offset = 0;
        while let Some(buffer) = user_buffers.next(limit - offset) {
            task.mm.read_memory(buffer.address, &mut bytes[offset..(offset + buffer.length)])?;
            offset += buffer.length;
        }
        Ok(bytes.into())
    }

    /// Returns the number of bytes in the message.
    pub fn len(&self) -> usize {
        self.bytes.len()
    }

    /// Splits the message data at `index`.
    ///
    /// After this call returns, at most `at` bytes will be stored in this `MessageData`, and any
    /// remaining bytes will be moved to the returned `MessageData`.
    pub fn split_off(&mut self, index: usize) -> Option<Self> {
        if index < self.len() {
            let message_data = MessageData { bytes: self.bytes.split_off(index) };
            Some(message_data)
        } else {
            None
        }
    }

    /// Returns a reference to the bytes in the packet.
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }

    /// Copies the message out to the user buffers in the given task.
    ///
    /// Returns the number of bytes that were read into the buffer.
    pub fn copy_to_user(
        &self,
        task: &Task,
        user_buffers: &mut UserBufferIterator<'_>,
    ) -> Result<usize, Errno> {
        let mut bytes_read = 0;
        while let Some(user_buffer) = user_buffers.next(self.bytes.len() - bytes_read) {
            let bytes_chunk = &self.bytes[bytes_read..(bytes_read + user_buffer.length)];
            task.mm.write_memory(user_buffer.address, bytes_chunk)?;
            bytes_read += user_buffer.length;
        }
        Ok(bytes_read)
    }
}

impl From<Vec<u8>> for MessageData {
    fn from(bytes: Vec<u8>) -> Self {
        Self { bytes }
    }
}
