// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::byteorder::{ByteOrder, NativeEndian};
use zerocopy::{AsBytes, FromBytes};

use crate::errno;
use crate::error;
use crate::fs::socket::SocketAddress;
use crate::fs::*;
use crate::task::Task;
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
    pub ancillary_data: Option<AncillaryData>,
}

impl Message {
    /// Creates a a new message with the provided message and ancillary data.
    pub fn new(
        data: MessageData,
        address: Option<SocketAddress>,
        ancillary_data: Option<AncillaryData>,
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
        Message { data, address: None, ancillary_data: None }
    }
}

impl From<Vec<u8>> for Message {
    fn from(data: Vec<u8>) -> Self {
        Self { data: data.into(), address: None, ancillary_data: None }
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
    /// Creates a new `AncillaryData` instance representing the data in `cmsghdr`.
    ///
    /// # Parameters
    /// - `task`: The task that is used to read the data from the `cmsghdr`.
    /// - `message_header`: The message header struct that this `AncillaryData` represents.
    pub fn new(task: &Task, message_header: cmsghdr) -> Result<Self, Errno> {
        if message_header.cmsg_level != SOL_SOCKET {
            return error!(EINVAL);
        }
        if message_header.cmsg_type != AF_UNIX as u32 {
            return error!(EINVAL);
        }
        // If the message header is not long enough to fit the required fields of the
        // control data, return EINVAL.
        if message_header.cmsg_len < cmsghdr::header_length() {
            return error!(EINVAL);
        }
        // If the message data length is greater than the number of bytes that can fit in the array,
        // return EINVAL.
        if message_header.data_length() > message_header.cmsg_data.len() {
            return error!(EINVAL);
        }

        Ok(AncillaryData::Unix(UnixControlData::new(task, message_header)?))
    }

    /// Returns a `cmsghdr` representation of this `AncillaryData`. This includes creating any
    /// objects (e.g., file descriptors) in `task`.
    pub fn into_cmsghdr(self, task: &Task) -> Result<cmsghdr, Errno> {
        match self {
            AncillaryData::Unix(control) => control.into_cmsghdr(task),
        }
    }

    /// Returns true iff the `size` is large enough to fit the message's header as well as all of
    /// its data.
    pub fn can_fit_all_data(&self, size: usize) -> bool {
        match self {
            AncillaryData::Unix(control) => control.can_fit_all_data(size),
        }
    }

    /// Returns true iff the `size` is large enough to fit the message's header as well as *any*
    /// amount of its data.
    pub fn can_fit_any_data(&self, size: usize) -> bool {
        match self {
            AncillaryData::Unix(control) => control.can_fit_any_data(size),
        }
    }
}

impl From<Vec<u8>> for AncillaryData {
    /// This is mainly used for testing purposes.
    fn from(data: Vec<u8>) -> Self {
        AncillaryData::Unix(UnixControlData::Security(data))
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
    pub fn new(task: &Task, message_header: cmsghdr) -> Result<Self, Errno> {
        match message_header.cmsg_type {
            SCM_RIGHTS => {
                // Compute the number of file descriptors that fit in the provided bytes.
                let bytes_per_file_descriptor = std::mem::size_of::<FdNumber>();
                let num_file_descriptors = message_header.data_length() / bytes_per_file_descriptor;

                // Get the files associated with the provided file descriptors.
                let files = (0..num_file_descriptors * bytes_per_file_descriptor)
                    .step_by(bytes_per_file_descriptor)
                    .map(|index| NativeEndian::read_i32(&message_header.cmsg_data[index..]))
                    .map(|fd| task.files.get(FdNumber::from_raw(fd)))
                    .collect::<Result<Vec<FileHandle>, Errno>>()?;

                Ok(UnixControlData::Rights(files))
            }
            SCM_CREDENTIALS => {
                if message_header.data_length() < std::mem::size_of::<ucred>() {
                    return error!(EINVAL);
                }

                let credentials =
                    ucred::read_from(&message_header.cmsg_data[..std::mem::size_of::<ucred>()])
                        .ok_or(errno!(EINVAL))?;
                Ok(UnixControlData::Credentials(credentials))
            }
            SCM_SECURITY => Ok(UnixControlData::Security(
                message_header.cmsg_data[..message_header.cmsg_len].to_owned(),
            )),
            _ => return error!(EINVAL),
        }
    }

    /// Constructs a cmsghdr for this control data, with a destination of `task`.
    ///
    /// The provided `task` is used to create any required file descriptors, etc.
    pub fn into_cmsghdr(self, task: &Task) -> Result<cmsghdr, Errno> {
        let mut cmsg_data = [0u8; SCM_MAX_FD * 4];
        let (cmsg_type, cmsg_len) = match self {
            UnixControlData::Rights(files) => {
                let fds: Vec<FdNumber> = files
                    .iter()
                    .map(|file| task.files.add_with_flags(file.clone(), FdFlags::empty()))
                    .collect::<Result<Vec<FdNumber>, Errno>>()?;
                let fd_bytes = fds.as_bytes();

                for (index, byte) in fd_bytes.iter().enumerate() {
                    cmsg_data[index] = *byte;
                }
                (SCM_RIGHTS, fd_bytes.len())
            }
            UnixControlData::Credentials(credentials) => {
                let bytes = credentials.as_bytes();
                for (index, byte) in bytes.iter().enumerate() {
                    cmsg_data[index] = *byte;
                }
                (SCM_CREDENTIALS, bytes.len())
            }
            UnixControlData::Security(string) => {
                for (index, byte) in string.iter().enumerate() {
                    cmsg_data[index] = *byte;
                }
                (SCM_SECURITY, string.len())
            }
        };

        Ok(cmsghdr {
            cmsg_len: cmsg_len + cmsghdr::header_length(),
            cmsg_level: SOL_SOCKET,
            cmsg_type,
            cmsg_data,
        })
    }

    /// Returns true iff the `size` is large enough to fit the message's header as well as all of
    /// its data.
    pub fn can_fit_all_data(&self, size: usize) -> bool {
        let data_length = match self {
            UnixControlData::Rights(files) => files.len() * std::mem::size_of::<FdNumber>(),
            UnixControlData::Credentials(_credentials) => std::mem::size_of::<ucred>(),
            UnixControlData::Security(string) => string.len(),
        };
        data_length + cmsghdr::header_length() <= size
    }

    /// Returns true iff the `size` is large enough to fit the message's header as well as *any*
    /// amount of its data.
    ///
    /// For example, when this message contains file descriptors, this will return true as long as
    /// `size` can fit both the header and at least one file descriptor.
    pub fn can_fit_any_data(&self, size: usize) -> bool {
        let data_length = match self {
            UnixControlData::Rights(_files) => std::mem::size_of::<FdNumber>(),
            UnixControlData::Credentials(_credentials) => std::mem::size_of::<ucred>(),
            UnixControlData::Security(string) => string.len(),
        };
        data_length + cmsghdr::header_length() <= size
    }
}

/// A `Packet` stores an arbitrary sequence of bytes.
#[derive(Clone, PartialEq, Debug, Default)]
pub struct MessageData {
    /// The bytes in this packet.
    bytes: Vec<u8>,
}

impl MessageData {
    /// Returns true if data is empty.
    pub fn is_empty(&self) -> bool {
        self.bytes.is_empty()
    }

    /// Returns the number of bytes in the message.
    pub fn len(&self) -> usize {
        self.bytes.len()
    }

    /// Splits the message data at `index`.
    ///
    /// After this call returns, at most `at` bytes will be stored in this `MessageData`, and any
    /// remaining bytes will be moved to the returned `MessageData`.
    pub fn split_off(&mut self, index: usize) -> Self {
        let mut message_data = MessageData::default();
        if index < self.len() {
            message_data.bytes = self.bytes.split_off(index);
        }
        message_data
    }

    /// Returns a reference to the bytes in the packet.
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
}

impl From<Vec<u8>> for MessageData {
    fn from(bytes: Vec<u8>) -> Self {
        Self { bytes }
    }
}
