// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The constant and type definitions in this file must all match
// //zircon/system/public/zircon/processargs.h
//
// TODO: Figure out a more foolproof way to keep these in sync, e.g. bindgen from the header
// directly, or maybe a new Tricium linter like IfThisThenThat.

use {
    failure::Fail, fuchsia_runtime::HandleInfo, fuchsia_zircon as zx, std::convert::TryFrom,
    std::ffi::CString, std::fmt, std::mem, std::num, zerocopy::AsBytes,
};

/// Possible errors that can occur during processargs startup message construction
#[allow(missing_docs)] // No docs on individual error variants.
#[derive(Fail, Debug)]
pub enum ProcessargsError {
    TryFromInt(#[cause] num::TryFromIntError),
    SizeTooLarge(usize),
}

impl ProcessargsError {
    /// Returns an appropriate zx::Status code for the given error.
    pub fn as_zx_status(&self) -> zx::Status {
        match self {
            ProcessargsError::TryFromInt(_) | ProcessargsError::SizeTooLarge(_) => {
                zx::Status::INVALID_ARGS
            }
        }
    }
}

// Can't use macro-based failure Display derive with the _MAX_MSG_BYTES argument below
impl fmt::Display for ProcessargsError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ProcessargsError::TryFromInt(e) => {
                write!(f, "Value > u32 MAX when building processargs message: {}", e)
            }
            ProcessargsError::SizeTooLarge(v) => write!(
                f,
                "Cannot build processargs message, size too large ({} > {})",
                v,
                zx::sys::ZX_CHANNEL_MAX_MSG_BYTES
            ),
        }
    }
}

const ZX_PROCARGS_PROTOCOL: u32 = 0x4150585d;
const ZX_PROCARGS_VERSION: u32 = 0x00001000;

/// Header for bootstrap message following the processargs protocol.
#[derive(AsBytes, Default)]
#[repr(C)]
struct MessageHeader {
    // Protocol and version identifiers to allow for different process start message protocols and
    // versioning of the same.
    protocol: u32,
    version: u32,

    // Offset from start of message to handle info array, which contains one HandleInfo as a u32
    // per handle passed along with the message.
    handle_info_off: u32,

    // Offset from start of message to arguments and count of arguments. Arguments are provided as
    // a set of null-terminated UTF-8 strings, one after the other.
    args_off: u32,
    args_num: u32,

    // Offset from start of message to environment strings and count of them.  Environment entries
    // are provided as a set of null-terminated UTF-8 strings, one after the other. Canonically
    // each string has the form "NAME=VALUE", but nothing enforces this.
    environ_off: u32,
    environ_num: u32,

    // Offset from start of message to namespace path strings and count of them. These strings are
    // packed similar to the argument strings, but are referenced by NamespaceDirectory (PA_NS_DIR)
    // handle table entries and used to set up namespaces.
    //
    // Specifically: In a handle table entry with HandleType of NamespaceDirectory (PA_NS_DIR), the
    // u16 handle info argument is an index into this name table.
    names_off: u32,
    names_num: u32,
}

/// A container for a single startup handle, containing a handle and metadata. Used as an input to
/// [ProcessBuilder::add_handles()].
///
/// [ProcessBuilder::add_handles()]: crate::ProcessBuilder::add_handles()
pub struct StartupHandle {
    /// A handle.
    pub handle: zx::Handle,

    /// Handle metadata. See [fuchsia_runtime::HandleInfo].
    pub info: HandleInfo,
}

#[derive(Default)]
pub struct MessageContents {
    pub args: Vec<CString>,
    pub environment_vars: Vec<CString>,
    pub namespace_paths: Vec<CString>,
    pub handles: Vec<StartupHandle>,
}

/// A bootstrap message following the processargs protocol.
///
/// See [//zircon/docs/program_loading.md#The-processargs-protocol][program_loading.md] or
/// [//zircon/system/public/zircon/processargs.h][processargs] for more details.
///
/// [program_loading.md]: https://fuchsia.googlesource.com/fuchsia/+/master/zircon/docs/program_loading.md#The-processargs-protocol
/// [processargs]: https://fuchsia.googlesource.com/fuchsia/+/master/zircon/system/public/zircon/processargs.h
pub struct Message {
    bytes: Vec<u8>,
    handles: Vec<zx::Handle>,
}

// Return type of fuchsia_runtime::HandleInfo::as_raw(), checked with static assert below.
type HandleInfoRaw = u32;

impl Message {
    /// Create a new bootstrap message using the given contents.
    pub fn build(contents: MessageContents) -> Result<Message, ProcessargsError> {
        let (header, size) = Self::build_header(&contents)?;

        let mut data = Vec::with_capacity(size);
        data.extend_from_slice(header.as_bytes());

        // Sanity check length against the offsets in the header as we go. Failures are bugs in
        // this code and serious enough to panic rather than continue, hence the asserts rather
        // than returning an Err().
        assert!(data.len() == header.handle_info_off as usize);
        let mut handles = Vec::with_capacity(contents.handles.len());
        for handle in contents.handles {
            let raw_info = handle.info.as_raw();
            static_assertions::assert_eq_size_val!(raw_info, 0 as HandleInfoRaw);

            data.extend_from_slice(&raw_info.to_ne_bytes());
            handles.push(handle.handle);
        }

        assert!(data.len() == header.args_off as usize);
        for arg in &contents.args {
            data.extend_from_slice(arg.as_bytes_with_nul());
        }

        assert!(data.len() == header.environ_off as usize);
        for var in &contents.environment_vars {
            data.extend_from_slice(var.as_bytes_with_nul());
        }

        assert!(data.len() == header.names_off as usize);
        for path in &contents.namespace_paths {
            data.extend_from_slice(path.as_bytes_with_nul());
        }

        // Sanity check final message size.
        assert!(data.len() == size);
        Ok(Message { bytes: data, handles })
    }

    /// Calculate the size that a bootstrap message will be if created using the given contents.
    ///
    /// Note that the size returned is only for the message data and does not include the size of
    /// the handles themselves, only the handle info in the message.
    pub fn calculate_size(contents: &MessageContents) -> Result<usize, ProcessargsError> {
        let (_, size) = Self::build_header(contents)?;
        Ok(size)
    }

    /// Builds the processargs message header for the given config, as well as calculates the total
    /// message size.
    fn build_header(config: &MessageContents) -> Result<(MessageHeader, usize), ProcessargsError> {
        let mut header = MessageHeader {
            protocol: ZX_PROCARGS_PROTOCOL,
            version: ZX_PROCARGS_VERSION,
            ..Default::default()
        };

        let mut size = mem::size_of_val(&header);
        let mut f = || {
            header.handle_info_off = u32::try_from(size)?;
            size += mem::size_of::<HandleInfoRaw>() * config.handles.len();

            header.args_off = u32::try_from(size)?;
            header.args_num = u32::try_from(config.args.len())?;
            for arg in &config.args {
                size += arg.as_bytes_with_nul().len();
            }

            header.environ_off = u32::try_from(size)?;
            header.environ_num = u32::try_from(config.environment_vars.len())?;
            for var in &config.environment_vars {
                size += var.as_bytes_with_nul().len();
            }

            header.names_off = u32::try_from(size)?;
            header.names_num = u32::try_from(config.namespace_paths.len())?;
            for path in &config.namespace_paths {
                size += path.as_bytes_with_nul().len();
            }
            Ok(())
        };
        f().map_err(|e| ProcessargsError::TryFromInt(e))?;

        if size > zx::sys::ZX_CHANNEL_MAX_MSG_BYTES as usize {
            return Err(ProcessargsError::SizeTooLarge(size));
        }
        Ok((header, size))
    }

    /// Write the processargs message to the provided channel.
    pub fn write(self, channel: &zx::Channel) -> Result<(), zx::Status> {
        let mut handles = self.handles;
        channel.write(self.bytes.as_slice(), &mut handles)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, failure::Error, fuchsia_runtime::HandleType, fuchsia_zircon::HandleBased};

    #[test]
    fn test_build_and_write_message() -> Result<(), Error> {
        // We need some dummy handles to use in the message below, since you can't send an invalid
        // handle in a channel. We just use VMOs since they're easy to create, even though they're
        // not semantically valid for a processargs handle type like PA_NS_DIR.
        let (dum0, dum1, dum2) = (zx::Vmo::create(1)?, zx::Vmo::create(1)?, zx::Vmo::create(1)?);

        let config = MessageContents {
            args: vec![CString::new("arg1")?, CString::new("arg2")?, CString::new("arg3")?],
            environment_vars: vec![CString::new("FOO=BAR")?],
            namespace_paths: vec![CString::new("/data")?, CString::new("/pkg")?],
            handles: vec![
                StartupHandle {
                    handle: dum0.into_handle(),
                    info: HandleInfo::new(HandleType::User1, 0x1234),
                },
                StartupHandle {
                    handle: dum1.into_handle(),
                    info: HandleInfo::new(HandleType::NamespaceDirectory, 0),
                },
                StartupHandle {
                    handle: dum2.into_handle(),
                    info: HandleInfo::new(HandleType::NamespaceDirectory, 1),
                },
            ],
        };
        let calculated_size = Message::calculate_size(&config)?;
        let message = Message::build(config)?;
        assert_eq!(calculated_size, message.bytes.len());

        // Write the message into a channel, read it back from the other end.
        let (chan_wr, chan_rd) = zx::Channel::create()?;
        message.write(&chan_wr)?;
        let mut read_buf = zx::MessageBuf::new();
        chan_rd.read(&mut read_buf)?;

        // concat! doesn't work for byte literals and there's no concat_bytes! (yet), so we just
        // build this in a Vec instead since it's a test.
        let mut correct = Vec::new();
        correct.extend_from_slice(b"\x5d\x58\x50\x41"); // protocol
        correct.extend_from_slice(b"\x00\x10\x00\x00"); // version
        correct.extend_from_slice(b"\x24\x00\x00\x00"); // handle_info_off
        correct.extend_from_slice(b"\x30\x00\x00\x00"); // args_off
        correct.extend_from_slice(b"\x03\x00\x00\x00"); // args_num
        correct.extend_from_slice(b"\x3F\x00\x00\x00"); // environ_off
        correct.extend_from_slice(b"\x01\x00\x00\x00"); // environ_num
        correct.extend_from_slice(b"\x47\x00\x00\x00"); // names_off
        correct.extend_from_slice(b"\x02\x00\x00\x00"); // names_num
        correct.extend_from_slice(b"\xF1\x00\x34\x12"); // handle info
        correct.extend_from_slice(b"\x20\x00\x00\x00"); //
        correct.extend_from_slice(b"\x20\x00\x01\x00"); //
        correct.extend_from_slice(b"arg1\0"); // args
        correct.extend_from_slice(b"arg2\0"); //
        correct.extend_from_slice(b"arg3\0"); //
        correct.extend_from_slice(b"FOO=BAR\0"); // environ
        correct.extend_from_slice(b"/data\0"); // namespace paths
        correct.extend_from_slice(b"/pkg\0");

        assert_eq!(read_buf.bytes(), correct.as_bytes());
        Ok(())
    }
}
