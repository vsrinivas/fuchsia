//! A library to decode and encode headers for the
//! [gzip format](http://www.gzip.org/zlib/rfc-gzip.html).
//! The library also contains a reader absctraction over a CRC checksum hasher.
//!
//! A file in the gzip format contains a gzip header, a number of compressed data blocks in the
//! [DEFLATE](http://www.gzip.org/zlib/rfc-deflate.html) format, and ends with the CRC32-checksum
//! (in the IEEE format) and number of bytes (modulo `2^32`) of the uncompressed data.
//!
//! The gzip header is purely a set of metadata, and doesn't have any impact on the decoding of the
//! compressed data other than the fact that `DEFLATE`-encoded data with a gzip-header is
//! checked using the CRC32 algorithm.
//!
//! This library is based on the gzip header functionality in the
//! [flate2](https://crates.io/crates/flate2) crate.

extern crate crc;

mod crc_reader;

use std::borrow::Cow;
use std::ffi::CString;
use std::{env, io, time};
use std::io::Read;
use std::fmt;
use std::default::Default;

pub use crc_reader::{Crc, CrcReader};

static FHCRC: u8 = 1 << 1;
static FEXTRA: u8 = 1 << 2;
static FNAME: u8 = 1 << 3;
static FCOMMENT: u8 = 1 << 4;

/// An enum describing the different OS types described in the gzip format.
/// See http://www.gzip.org/format.txt (Additionally, the Apple(19) value is defined in the zlib
/// library).
#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum FileSystemType {
    ///MS-DOS/old FAT filesystem
    Fat = 0,
    Amiga = 1,
    Vms = 2,
    Unix = 3,
    Vcms = 4,
    AtariTos = 5,
    Hpfs = 6,
    /// Used for apple platforms. Newer encoders may use 19 instead for modern systems.
    Macintosh = 7,
    Zsystem = 8,
    Cpm = 9,
    /// This is used for Windows/NTFS in zlib newer than 1.2.11, but not in gzip due to following
    /// updates to the ZIP format.
    /// See https://github.com/madler/zlib/issues/235 and
    /// https://github.com/madler/zlib/commit/ce12c5cd00628bf8f680c98123a369974d32df15
    Tops20OrNTFS = 10,
    /// Used for Windows platforms for older zlib versions and other encoders.
    NTFS = 11,
    SmsQdos = 12,
    Riscos = 13,
    /// Newer fat filesystems (i.e FAT32).
    Vfat = 14,
    Mvs = 15,
    Beos = 16,
    TandemNsk = 17,
    Theos = 18,
    /// Modern apple platforms.
    /// Defined in the zlib library (see zutil.h)
    Apple = 19,
    Unknown = 255,
}

impl FileSystemType {
    /// Get the raw byte value of this `FileSystemType` variant.
    pub fn as_u8(&self) -> u8 {
        *self as u8
    }

    /// Get the corresponding `ExtraFlags` value from a raw byte.
    ///
    /// Returns `FileSystemType::Unknown` (defined as 255 as that is the value used in the
    /// specification for `Unknown`) if the value is not one of the currently known types
    /// (Which currently means any value > 19).
    pub fn from_u8(value: u8) -> FileSystemType {
        use FileSystemType::*;
        match value {
            0 => Fat,
            1 => Amiga,
            2 => Vms,
            3 => Unix,
            4 => Vcms,
            5 => AtariTos,
            6 => Hpfs,
            7 => Macintosh,
            8 => Zsystem,
            9 => Cpm,
            10 => Tops20OrNTFS,
            11 => NTFS,
            12 => SmsQdos,
            13 => Riscos,
            14 => Vfat,
            15 => Mvs,
            16 => Beos,
            17 => TandemNsk,
            18 => Theos,
            19 => Apple,
            _ => Unknown,
        }
    }
}

impl fmt::Display for FileSystemType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use FileSystemType::*;
        match *self {
            Fat => "FAT filesystem (MS-DOS, OS/2, NT/Win32)",
            Amiga => "Amiga",
            Vms => "VMS or OpenVMS",
            Unix => "Unix type system/Linux",
            Vcms => "VM/CMS",
            AtariTos => "Atari TOS",
            Hpfs => "HPFS filesystem (OS/2, NT)",
            Macintosh => "Macintosh operating system (Classic Mac OS, OS/X, macOS, iOS etc.)",
            Zsystem => "Z-System",
            Cpm => "CP/M",
            Tops20OrNTFS => "NTFS (New zlib versions) or TOPS-20",
            NTFS => "NTFS",
            SmsQdos => "SMS/QDOS",
            Riscos => "Acorn RISC OS",
            Vfat => "VFAT file system (Win95, NT)",
            Mvs => "MVS or PRIMOS",
            Beos => "BeOS",
            TandemNsk => "Tandem/NSK",
            Theos => "THEOS",
            Apple => "macOS, OS/X, iOS or watchOS",
            _ => "Unknown or unset",
        }.fmt(f)
    }
}

/// Valid values for the extra flag in the gzip specification.
///
/// This is a field to be used by the compression methods. For deflate, which is the only
/// specified compression method, this is a value indicating the level of compression of the
/// contained compressed data. This value does not have to correspond to the actual compression
/// level of the contained data, it's only a hint that the the encoder may set.
#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum ExtraFlags {
    Default = 0,
    MaximumCompression = 2,
    FastestCompression = 4,
}

impl ExtraFlags {
    /// Get the corresponding `ExtraFlags` value from a raw byte.
    ///
    /// Returns `ExtraFlags::Default` (defined as 0 by the gzip specification) for values other than
    /// 2 and 4.
    pub fn from_u8(value: u8) -> ExtraFlags {
        use ExtraFlags::*;
        match value {
            2 => MaximumCompression,
            4 => FastestCompression,
            _ => Default,
        }
    }

    /// Get the raw byte value of this `ExtraFlags` variant.
    pub fn as_u8(&self) -> u8 {
        *self as u8
    }
}

impl fmt::Display for ExtraFlags {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            ExtraFlags::Default => "No extra flags (Default) or unknown.",
            ExtraFlags::MaximumCompression => "Maximum compression algorithm (DEFLATE).",
            ExtraFlags::FastestCompression => "Fastest compression algorithm (DEFLATE)",
        }.fmt(f)
    }
}

impl Default for ExtraFlags {
    fn default() -> ExtraFlags {
        ExtraFlags::Default
    }
}

/// A builder structure to create a new gzip header.
///
/// This structure controls header configuration options such as the filename.
#[derive(Debug, Default, Clone, Eq, PartialEq)]
pub struct GzBuilder {
    extra: Option<Vec<u8>>,
    filename: Option<CString>,
    comment: Option<CString>,
    // Whether this should be signed is a bit unclear, the gzip spec says mtime is in the unix
    // time format, which is normally signed, however zlib seems to use an unsigned long for this
    // field.
    mtime: u32,
    os: Option<FileSystemType>,
    xfl: ExtraFlags,
}

impl GzBuilder {
    /// Create a new blank builder with no header by default.
    pub fn new() -> GzBuilder {
        GzBuilder {
            extra: None,
            filename: None,
            comment: None,
            mtime: 0,
            os: None,
            xfl: ExtraFlags::Default,
        }
    }

    /// Configure the `mtime` field in the gzip header.
    pub fn mtime(mut self, mtime: u32) -> GzBuilder {
        self.mtime = mtime;
        self
    }

    /// Configure the `extra` field in the gzip header.
    pub fn extra<T: Into<Vec<u8>>>(mut self, extra: T) -> GzBuilder {
        self.extra = Some(extra.into());
        self
    }

    /// Configure the `filename` field in the gzip header.
    ///
    /// # Panics
    /// Panics if the filename argument contains a byte with the value 0.
    pub fn filename<T: Into<Vec<u8>>>(mut self, filename: T) -> GzBuilder {
        self.filename = Some(CString::new(filename).unwrap());
        self
    }

    /// Configure the `comment` field in the gzip header.
    ///
    /// # Panics
    /// Panics if the comment argument contains a byte with the value 0.
    pub fn comment<T: Into<Vec<u8>>>(mut self, comment: T) -> GzBuilder {
        self.comment = Some(CString::new(comment).unwrap());
        self
    }

    /// Configure the `os` field in the gzip header.
    ///
    /// This is taken from `std::env::consts::OS` if not set explicitly.
    pub fn os(mut self, os: FileSystemType) -> GzBuilder {
        self.os = Some(os);
        self
    }

    /// Configure the `xfl` field in the gzip header.
    ///
    /// The default is `ExtraFlags::Default` (meaning not set).
    pub fn xfl(mut self, xfl: ExtraFlags) -> GzBuilder {
        self.xfl = xfl;
        self
    }

    /// Transforms this builder structure into a raw vector of bytes, setting the `XFL` field to the
    /// value specified by `lvl`.
    pub fn into_header_xfl(mut self, lvl: ExtraFlags) -> Vec<u8> {
        self.xfl = lvl;
        self.into_header()
    }

    /// Transforms this builder structure into a raw vector of bytes.
    pub fn into_header(self) -> Vec<u8> {
        self.into_header_inner(false)
    }

    /// Transforms this builder structure into a raw vector of bytes.
    pub fn into_header_with_checksum(self) -> Vec<u8> {
        self.into_header_inner(true)
    }

    fn into_header_inner(self, use_crc: bool) -> Vec<u8> {
        let GzBuilder {
            extra,
            filename,
            comment,
            mtime,
            os,
            xfl,
        } = self;
        let os = match os {
            Some(f) => f,
            // Set the OS based on the system the binary is compiled for if not set,
            // as this is a required field.
            // These defaults are taken from what modern zlib uses, which are not the same as
            // what's used in flate2.
            None => match env::consts::OS {
                "linux" | "freebsd" | "dragonfly" | "netbsd" | "openbsd" | "solaris" | "bitrig" => {
                    FileSystemType::Unix
                }
                "macos" => FileSystemType::Apple,
                "win32" => FileSystemType::Tops20OrNTFS,
                _ => FileSystemType::Unknown,
            },

        };
        let mut flg = 0;
        if use_crc {
            flg |= FHCRC;
        };
        let mut header = vec![0u8; 10];

        if let Some(v) = extra {
            flg |= FEXTRA;
            header.push((v.len()/* >> 0*/) as u8);
            header.push((v.len() >> 8) as u8);
            header.extend(v);
        }

        if let Some(filename) = filename {
            flg |= FNAME;
            header.extend(filename.as_bytes_with_nul().iter().cloned());
        }

        if let Some(comment) = comment {
            flg |= FCOMMENT;
            header.extend(comment.as_bytes_with_nul().iter().cloned());
        }

        header[0] = 0x1f;
        header[1] = 0x8b;
        header[2] = 8;
        header[3] = flg;
        header[4] = mtime /*>> 0*/ as u8;
        header[5] = (mtime >> 8) as u8;
        header[6] = (mtime >> 16) as u8;
        header[7] = (mtime >> 24) as u8;
        header[8] = xfl.as_u8();
        header[9] = os.as_u8();

        if use_crc {
            let mut crc = Crc::new();
            crc.update(&header);
            let checksum = crc.sum() as u16;
            header.extend(&[checksum as u8, (checksum >> 8) as u8]);
        }

        header
    }
}

/// A structure representing the raw header of a gzip stream.
///
/// The header can contain metadata about the file that was compressed, if
/// present.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct GzHeader {
    extra: Option<Vec<u8>>,
    filename: Option<Vec<u8>>,
    comment: Option<Vec<u8>>,
    mtime: u32,
    os: u8,
    xfl: u8,
}

impl GzHeader {
    /// Returns the `filename` field of this gzip header, if present.
    ///
    /// The `filename` field the gzip header is supposed to be stored using ISO 8859-1 (LATIN-1)
    /// encoding and be zero-terminated if following the specification.
    pub fn filename(&self) -> Option<&[u8]> {
        self.filename.as_ref().map(|s| &s[..])
    }

    /// Returns the `extra` field of this gzip header, if present.
    pub fn extra(&self) -> Option<&[u8]> {
        self.extra.as_ref().map(|s| &s[..])
    }

    /// Returns the `comment` field of this gzip stream's header, if present.
    ///
    /// The `comment` field in the gzip header is supposed to be stored using ISO 8859-1 (LATIN-1)
    /// encoding and be zero-terminated if following the specification.
    pub fn comment(&self) -> Option<&[u8]> {
        self.comment.as_ref().map(|s| &s[..])
    }

    /// Returns the `mtime` field of this gzip header.
    ///
    /// This gives the most recent modification time of the contained file, or alternatively
    /// the timestamp of when the file was compressed if the data did not come from a file, or
    /// a timestamp was not available when compressing. The time is specified the Unix format,
    /// that is: seconds since 00:00:00 GMT, Jan. 1, 1970. (Not that this may cause problems for
    /// MS-DOS and other systems that use local rather than Universal time.)
    /// An `mtime` value of 0 means that the timestamp is not set.
    pub fn mtime(&self) -> u32 {
        self.mtime
    }

    /// Returns the `mtime` field of this gzip header as a `SystemTime` if present.
    ///
    /// Returns `None` if the `mtime` is not set, i.e 0.
    /// See [`mtime`](#method.mtime) for more detail.
    pub fn mtime_as_datetime(&self) -> Option<time::SystemTime> {
        if self.mtime == 0 {
            None
        } else {
            let duration = time::Duration::new(u64::from(self.mtime), 0);
            let datetime = time::UNIX_EPOCH + duration;
            Some(datetime)
        }
    }

    /// Returns the `os` field of this gzip stream's header.
    pub fn os(&self) -> u8 {
        self.os
    }

    /// Returns the `xfl` field of this gzip stream's header.
    pub fn xfl(&self) -> u8 {
        self.xfl
    }
}

#[inline]
fn into_string(data: Option<&[u8]>) -> Cow<str> {
    data.map_or_else(
        || Cow::Borrowed("(Not set)"),
        |d| String::from_utf8_lossy(d),
    )
}

impl fmt::Display for GzHeader {
    /// Crudely display the contents of the header
    ///
    /// Note that filename/commend are required to be ISO 8859-1 (LATIN-1) encoded by the spec,
    /// however to avoid dragging in dependencies we simply interpret them as UTF-8.
    /// This may result in garbled output if the names contain special characters.
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "Filename: {}\n\
             Comment: {}\n\
             Extra: {:?}\n\
             mtime: {}\n\
             os: {}\n\
             xfl: {}",
            into_string(self.filename()),
            into_string(self.comment()),
            // We display extra as raw bytes for now.
            self.extra,
            self.mtime,
            FileSystemType::from_u8(self.os),
            ExtraFlags::Default, //ExtraFlags::from_u8(self.xfl),
        )
    }
}

fn corrupt() -> io::Error {
    io::Error::new(
        io::ErrorKind::InvalidInput,
        "corrupt gzip stream does not have a matching header checksum",
    )
}

fn bad_header() -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, "invalid gzip header")
}

/// Try to read a little-endian u16 from the provided reader.
fn read_le_u16<R: Read>(r: &mut R) -> io::Result<u16> {
    let mut b = [0; 2];
    try!(r.read_exact(&mut b));
    Ok((b[0] as u16) | ((b[1] as u16) << 8))
}

/// Try to read a gzip header from the provided reader.
///
/// Returns a `GzHeader` with the fields filled out if sucessful, or an `io::Error` with
/// `ErrorKind::InvalidInput` if decoding of the header.
///
/// Note that a gzip steam can contain multiple "members". Each member contains a header,
/// followed by compressed data and finally a checksum and byte count.
/// This method will only read the header for the "member" at the start of the stream.
pub fn read_gz_header<R: Read>(r: &mut R) -> io::Result<GzHeader> {
    let mut crc_reader = CrcReader::new(r);
    let mut header = [0; 10];
    try!(crc_reader.read_exact(&mut header));

    // `ID1` and `ID2` are fixed values to identify a gzip file.
    let id1 = header[0];
    let id2 = header[1];
    if id1 != 0x1f || id2 != 0x8b {
        return Err(bad_header());
    }
    // `CM` describes the compression method. Currently only method 8 (DEFLATE) is specified.
    // by the gzip format.
    let cm = header[2];
    if cm != 8 {
        return Err(bad_header());
    }

    // `FLG` the bits in this field indicates whether the `FTEXT`, `FHCRC`, `FEXTRA`, `FNAME` and
    // `FCOMMENT` fields are present in the header.
    let flg = header[3];
    let mtime = (header[4] as u32/* << 0*/) | ((header[5] as u32) << 8) |
        ((header[6] as u32) << 16) | ((header[7] as u32) << 24);
    // `XFL` describes the compression level used by the encoder. (May not actually
    // match what the encoder used and has no impact on decompression.)
    let xfl = header[8];
    // `os` describes what type of operating system/file system the file was created on.
    let os = header[9];

    let extra = if flg & FEXTRA != 0 {
        // Length of the FEXTRA field.
        let xlen = try!(read_le_u16(&mut crc_reader));
        let mut extra = vec![0; xlen as usize];
        try!(crc_reader.read_exact(&mut extra));
        Some(extra)
    } else {
        None
    };
    let filename = if flg & FNAME != 0 {
        // wow this is slow
        let mut b = Vec::new();
        for byte in crc_reader.by_ref().bytes() {
            let byte = try!(byte);
            if byte == 0 {
                break;
            }
            b.push(byte);
        }
        Some(b)
    } else {
        None
    };
    let comment = if flg & FCOMMENT != 0 {
        // wow this is slow
        let mut b = Vec::new();
        for byte in crc_reader.by_ref().bytes() {
            let byte = try!(byte);
            if byte == 0 {
                break;
            }
            b.push(byte);
        }
        Some(b)
    } else {
        None
    };

    // If the `FHCRC` flag is set, the header contains a two-byte CRC16 checksum of the header bytes
    // that needs to be validated.
    if flg & FHCRC != 0 {
        let calced_crc = crc_reader.crc().sum() as u16;
        let stored_crc = try!(read_le_u16(&mut crc_reader));
        if calced_crc != stored_crc {
            return Err(corrupt());
        }
    }

    Ok(GzHeader {
        extra: extra,
        filename: filename,
        comment: comment,
        mtime: mtime,
        os: os,
        xfl: xfl,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    fn roundtrip_inner(use_crc: bool) {
        const COMMENT: &'static [u8] = b"Comment";
        const FILENAME: &'static [u8] = b"Filename";
        const MTIME: u32 = 12345;
        const OS: FileSystemType = FileSystemType::NTFS;
        const XFL: ExtraFlags = ExtraFlags::FastestCompression;

        let header = GzBuilder::new()
            .comment(COMMENT)
            .filename(FILENAME)
            .mtime(MTIME)
            .os(OS)
            .xfl(ExtraFlags::FastestCompression)
            .into_header_inner(use_crc);

        let mut reader = Cursor::new(header.clone());

        let header_read = read_gz_header(&mut reader).unwrap();

        assert_eq!(header_read.comment().unwrap(), COMMENT);
        assert_eq!(header_read.filename().unwrap(), FILENAME);
        assert_eq!(header_read.mtime(), MTIME);
        assert_eq!(header_read.os(), OS.as_u8());
        assert_eq!(header_read.xfl(), XFL.as_u8());
    }

    #[test]
    fn roundtrip() {
        roundtrip_inner(false);

    }

    #[test]
    fn roundtrip_with_crc() {
        roundtrip_inner(true);
    }

    #[test]
    fn filesystem_enum() {
        for n in 0..20 {
            assert_eq!(n, FileSystemType::from_u8(n).as_u8());
        }

        for n in 20..(u8::max_value() as u16) + 1 {
            assert_eq!(FileSystemType::from_u8(n as u8), FileSystemType::Unknown);
        }
    }
}
