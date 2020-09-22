// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bitflags::bitflags,
    fuchsia_zircon as zx,
    num_derive::FromPrimitive,
    num_traits::cast::FromPrimitive,
    owning_ref::OwningRef,
    static_assertions::assert_eq_size,
    std::fmt,
    std::mem,
    thiserror::Error,
    zerocopy::{FromBytes, LayoutVerified},
};

/// Possible errors that can occur during ELF parsing.
#[allow(missing_docs)] // No docs on individual error variants.
#[derive(Error, Debug)]
pub enum ElfParseError {
    #[error("Failed to read ELF from VMO: {}", _0)]
    ReadError(zx::Status),
    #[error("Parse error: {}", _0)]
    ParseError(&'static str),
    #[error("Invalid ELF file header: {}", _0)]
    InvalidFileHeader(&'static str),
    #[error("Invalid ELF program header: {}", _0)]
    InvalidProgramHeader(&'static str),
    #[error("Multiple ELF program headers of type {} present", _0)]
    MultipleHeaders(SegmentType),
}

impl ElfParseError {
    /// Returns an appropriate zx::Status code for the given error.
    pub fn as_zx_status(&self) -> zx::Status {
        match self {
            ElfParseError::ReadError(s) => *s,
            // Not a great status to return for an invalid ELF but there's no great fit, and this
            // matches elf_load.
            ElfParseError::ParseError(_)
            | ElfParseError::InvalidFileHeader(_)
            | ElfParseError::InvalidProgramHeader(_) => zx::Status::NOT_FOUND,
            ElfParseError::MultipleHeaders(_) => zx::Status::NOT_FOUND,
        }
    }
}

trait Validate {
    fn validate(&self) -> Result<(), ElfParseError>;
}

#[derive(FromBytes, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct ElfIdent {
    pub magic: [u8; 4], // e_ident[EI_MAG0:EI_MAG3]
    pub class: u8,      // e_ident[EI_CLASS]
    pub data: u8,       // e_ident[EI_DATA]
    pub version: u8,    // e_ident[EI_VERSION]
    pub osabi: u8,      // e_ident[EI_OSABI]
    pub abiversion: u8, // e_ident[EI_ABIVERSION]
    pub pad: [u8; 7],   // e_ident[EI_PAD]
}

#[allow(unused)]
const EI_NIDENT: usize = 16;
assert_eq_size!(ElfIdent, [u8; EI_NIDENT]);

#[derive(FromPrimitive, Eq, PartialEq)]
#[repr(u8)]
pub enum ElfClass {
    Unknown = 0, // ELFCLASSNONE
    Elf32 = 1,   // ELFCLASS32
    Elf64 = 2,   // ELFCLASS64
}

#[derive(FromPrimitive, Eq, PartialEq)]
#[repr(u8)]
pub enum ElfDataEncoding {
    Unknown = 0,      // ELFDATANONE
    LittleEndian = 1, // ELFDATA2LSB
    BigEndian = 2,    // ELFDATA2MSB
}

#[derive(FromPrimitive, Eq, PartialEq)]
#[repr(u8)]
pub enum ElfVersion {
    Unknown = 0, // EV_NONE
    Current = 1, // EV_CURRENT
}

impl ElfIdent {
    pub fn class(&self) -> Result<ElfClass, u8> {
        ElfClass::from_u8(self.class).ok_or(self.class)
    }

    pub fn data(&self) -> Result<ElfDataEncoding, u8> {
        ElfDataEncoding::from_u8(self.data).ok_or(self.data)
    }

    pub fn version(&self) -> Result<ElfVersion, u8> {
        ElfVersion::from_u8(self.version).ok_or(self.version)
    }
}

#[derive(FromBytes, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct Elf64FileHeader {
    pub ident: ElfIdent,
    pub elf_type: u16,
    pub machine: u16,
    pub version: u32,
    pub entry: usize,
    pub phoff: usize,
    pub shoff: usize,
    pub flags: u32,
    pub ehsize: u16,
    pub phentsize: u16,
    pub phnum: u16,
    pub shentsize: u16,
    pub shnum: u16,
    pub shstrndx: u16,
}

#[derive(FromPrimitive, Copy, Clone, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum ElfType {
    Unknown = 0,      // ET_NONE
    Relocatable = 1,  // ET_REL
    Executable = 2,   // ET_EXEC
    SharedObject = 3, // ET_DYN
    Core = 4,         // ET_CORE
}

#[derive(FromPrimitive, Copy, Clone, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum ElfArchitecture {
    Unknown = 0,   // EM_NONE
    I386 = 3,      // EM_386
    ARM = 40,      // EM_ARM
    X86_64 = 62,   // EM_X86_64
    AARCH64 = 183, // EM_AARCH64
}

const ELF_MAGIC: [u8; 4] = *b"\x7fELF";

#[cfg(target_endian = "little")]
const NATIVE_ENCODING: ElfDataEncoding = ElfDataEncoding::LittleEndian;
#[cfg(target_endian = "big")]
const NATIVE_ENCODING: ElfDataEncoding = ElfDataEncoding::BigEndian;

#[cfg(target_arch = "x86_64")]
const CURRENT_ARCH: ElfArchitecture = ElfArchitecture::X86_64;
#[cfg(target_arch = "aarch64")]
const CURRENT_ARCH: ElfArchitecture = ElfArchitecture::AARCH64;

impl Elf64FileHeader {
    pub fn elf_type(&self) -> Result<ElfType, u16> {
        ElfType::from_u16(self.elf_type).ok_or(self.elf_type)
    }

    pub fn machine(&self) -> Result<ElfArchitecture, u16> {
        ElfArchitecture::from_u16(self.machine).ok_or(self.machine)
    }

    fn from_bytes(bytes: &[u8]) -> Result<LayoutVerified<&[u8], Elf64FileHeader>, ElfParseError> {
        LayoutVerified::new(bytes)
            .ok_or(ElfParseError::ParseError("Failed to parse ELF64 file header"))
    }
}

impl Validate for Elf64FileHeader {
    fn validate(&self) -> Result<(), ElfParseError> {
        if self.ident.magic != ELF_MAGIC {
            return Err(ElfParseError::InvalidFileHeader("Invalid ELF magic"));
        }
        if self.ident.class() != Ok(ElfClass::Elf64) {
            return Err(ElfParseError::InvalidFileHeader("Invalid ELF class"));
        }
        if self.ident.data() != Ok(NATIVE_ENCODING) {
            return Err(ElfParseError::InvalidFileHeader("Invalid ELF data encoding"));
        }
        if self.ident.version() != Ok(ElfVersion::Current) {
            return Err(ElfParseError::InvalidFileHeader("Invalid ELF version"));
        }
        if self.phentsize as usize != mem::size_of::<Elf64ProgramHeader>() {
            return Err(ElfParseError::InvalidFileHeader("Invalid ELF program header size"));
        }
        if self.phnum == std::u16::MAX {
            return Err(ElfParseError::InvalidFileHeader(
                "2^16 or more ELF program headers is unsupported",
            ));
        }
        if self.machine() != Ok(CURRENT_ARCH) {
            return Err(ElfParseError::InvalidFileHeader("Invalid ELF architecture"));
        }
        if self.elf_type() != Ok(ElfType::SharedObject) {
            return Err(ElfParseError::InvalidFileHeader(
                "Invalid or unsupported ELF type, only ET_DYN is supported",
            ));
        }
        Ok(())
    }
}

#[derive(FromBytes, Debug, Eq, PartialEq)]
#[repr(C)]
pub struct Elf64ProgramHeader {
    pub segment_type: u32,
    pub flags: u32,
    pub offset: usize,
    pub vaddr: usize,
    pub paddr: usize,
    pub filesz: u64,
    pub memsz: u64,
    pub align: u64,
}

#[derive(FromPrimitive, Copy, Clone, Debug, Eq, PartialEq)]
#[repr(u32)]
pub enum SegmentType {
    Unused = 0,            // PT_NULL
    Load = 1,              // PT_LOAD
    Dynamic = 2,           // PT_DYNAMIC
    Interp = 3,            // PT_INTERP
    GnuStack = 0x6474e551, // PT_GNU_STACK
}

bitflags! {
    pub struct SegmentFlags: u32 {
        const EXECUTE = 0b0001;
        const WRITE   = 0b0010;
        const READ    = 0b0100;
    }
}

impl fmt::Display for SegmentType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SegmentType::Unused => write!(f, "PT_NULL"),
            SegmentType::Load => write!(f, "PT_LOAD"),
            SegmentType::Dynamic => write!(f, "PT_DYNAMIC"),
            SegmentType::Interp => write!(f, "PT_INTERP"),
            SegmentType::GnuStack => write!(f, "PT_GNU_STACK"),
        }
    }
}

impl Elf64ProgramHeader {
    pub fn segment_type(&self) -> Result<SegmentType, u32> {
        SegmentType::from_u32(self.segment_type).ok_or(self.segment_type)
    }

    pub fn flags(&self) -> SegmentFlags {
        // Ignore bits that don't correspond to one of the flags included in SegmentFlags
        SegmentFlags::from_bits_truncate(self.flags)
    }
}

impl Validate for [Elf64ProgramHeader] {
    fn validate(&self) -> Result<(), ElfParseError> {
        let mut vaddr_high: usize = 0;
        for hdr in self {
            if hdr.filesz > hdr.memsz {
                return Err(ElfParseError::InvalidProgramHeader("filesz > memsz"));
            }

            match hdr.segment_type() {
                Ok(SegmentType::Load) => {
                    // Virtual addresses for PT_LOAD segments should not overlap.
                    if hdr.vaddr < vaddr_high {
                        return Err(ElfParseError::InvalidProgramHeader(
                            "Overlap in virtual addresses",
                        ));
                    }
                    vaddr_high = hdr.vaddr + hdr.memsz as usize;
                }
                Ok(SegmentType::GnuStack) => {
                    if hdr.flags().contains(SegmentFlags::EXECUTE) {
                        return Err(ElfParseError::InvalidProgramHeader(
                            "Fuchsia does not support executable stacks",
                        ));
                    }
                }
                // No specific validation to perform for these.
                Ok(SegmentType::Unused) | Ok(SegmentType::Interp) | Ok(SegmentType::Dynamic) => {}
                // Ignore segment types that we don't care about.
                Err(_) => {}
            }
        }
        Ok(())
    }
}

pub struct Elf64Headers {
    // These headers are read straight out of a VMO and then parsed with zerocopy, so we use
    // OwningRef to keep ownership of the underlying bytes and hold a reference to the parsed
    // structs that will be actually used. Public accessors provide access to the parsed headers
    // and hide this detail.
    file_header: OwningRef<Vec<u8>, Elf64FileHeader>,
    program_headers: Option<OwningRef<Vec<u8>, [Elf64ProgramHeader]>>,
    // Section headers are not parsed currently since they aren't needed for the current use case,
    // but could be added if needed.
}

impl Elf64Headers {
    pub fn from_vmo(vmo: &zx::Vmo) -> Result<Elf64Headers, ElfParseError> {
        // Read and parse the ELF file header from the VMO.
        let file_hdr_len = mem::size_of::<Elf64FileHeader>();
        let mut data = vec![0u8; file_hdr_len];
        vmo.read(&mut data[..], 0).map_err(|s| ElfParseError::ReadError(s))?;
        let data_oref = OwningRef::new(data);
        let file_header: OwningRef<Vec<u8>, Elf64FileHeader> =
            data_oref.try_map(|v| Elf64FileHeader::from_bytes(v).map(|lv| lv.into_ref()))?;
        file_header.validate()?;

        // Read and parse the ELF program headers from the VMO. Also support the degenerate case
        // where there are no program headers, which is valid ELF but probably not useful outside
        // tests.
        let mut program_headers = None;
        let phdrs_size = file_header.phnum as usize * mem::size_of::<Elf64ProgramHeader>();
        if phdrs_size > 0 {
            let mut phdrs_data = vec![0; phdrs_size];
            vmo.read(&mut phdrs_data[..], file_header.phoff as u64)
                .map_err(|s| ElfParseError::ReadError(s))?;
            let phdrs_data_oref = OwningRef::new(phdrs_data);
            let phdrs = phdrs_data_oref.try_map(|v| {
                LayoutVerified::new_slice(v)
                    .ok_or(ElfParseError::ParseError("Failed to parse ELF64 program headers"))
                    .map(|lv| lv.into_slice())
            })?;
            phdrs.validate()?;
            program_headers = Some(phdrs);
        }

        Ok(Elf64Headers { file_header, program_headers })
    }

    pub fn file_header(&self) -> &Elf64FileHeader {
        &*self.file_header
    }

    pub fn program_headers(&self) -> &[Elf64ProgramHeader] {
        match &self.program_headers {
            Some(own_ref) => &*own_ref,
            None => &[],
        }
    }

    /// Returns an iterator that yields all program headers of the given type.
    pub fn program_headers_with_type(
        &self,
        stype: SegmentType,
    ) -> impl Iterator<Item = &Elf64ProgramHeader> {
        self.program_headers().iter().filter(move |x| match x.segment_type() {
            Ok(t) => t == stype,
            _ => false,
        })
    }

    /// Returns 0 or 1 headers of the given type, or Err([ElfParseError::MultipleHeaders]) if more
    /// than 1 such header is present.
    pub fn program_header_with_type(
        &self,
        stype: SegmentType,
    ) -> Result<Option<&Elf64ProgramHeader>, ElfParseError> {
        let mut headers = self.program_headers_with_type(stype);
        let header = headers.next();
        if headers.next().is_some() {
            return Err(ElfParseError::MultipleHeaders(stype));
        }
        return Ok(header);
    }
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Error, fdio, std::fs::File};

    // These are specially crafted files that just contain a valid ELF64 file header but
    // nothing else.
    static HEADER_DATA_X86_64: &'static [u8] = include_bytes!("../test/elf_x86-64_file-header.bin");
    static HEADER_DATA_AARCH64: &'static [u8] =
        include_bytes!("../test/elf_aarch64_file-header.bin");
    #[cfg(target_arch = "x86_64")]
    static HEADER_DATA: &'static [u8] = HEADER_DATA_X86_64;
    #[cfg(target_arch = "aarch64")]
    static HEADER_DATA: &'static [u8] = HEADER_DATA_AARCH64;
    #[cfg(target_arch = "x86_64")]
    static HEADER_DATA_WRONG_ARCH: &'static [u8] = HEADER_DATA_AARCH64;
    #[cfg(target_arch = "aarch64")]
    static HEADER_DATA_WRONG_ARCH: &'static [u8] = HEADER_DATA_X86_64;

    #[test]
    fn test_parse_file_header() -> Result<(), Error> {
        let vmo = zx::Vmo::create(HEADER_DATA.len() as u64)?;
        vmo.write(&HEADER_DATA, 0)?;

        let headers = Elf64Headers::from_vmo(&vmo)?;
        assert_eq!(
            headers.file_header(),
            &Elf64FileHeader {
                ident: ElfIdent {
                    magic: ELF_MAGIC,
                    class: ElfClass::Elf64 as u8,
                    data: ElfDataEncoding::LittleEndian as u8,
                    version: ElfVersion::Current as u8,
                    osabi: 0,
                    abiversion: 0,
                    pad: [0; 7],
                },
                elf_type: ElfType::SharedObject as u16,
                machine: CURRENT_ARCH as u16,
                version: 1,
                entry: 0x10000,
                phoff: 0,
                shoff: 0,
                flags: 0,
                ehsize: mem::size_of::<Elf64FileHeader>() as u16,
                phentsize: mem::size_of::<Elf64ProgramHeader>() as u16,
                phnum: 0,
                shentsize: 0,
                shnum: 0,
                shstrndx: 0,
            }
        );
        assert_eq!(headers.program_headers().len(), 0);
        Ok(())
    }

    #[test]
    fn test_parse_wrong_arch() -> Result<(), Error> {
        let vmo = zx::Vmo::create(HEADER_DATA_WRONG_ARCH.len() as u64)?;
        vmo.write(&HEADER_DATA, 0)?;

        match Elf64Headers::from_vmo(&vmo) {
            Err(ElfParseError::InvalidFileHeader(msg)) => {
                assert_eq!(msg, "Invalid ELF architecture");
            }
            _ => {}
        }
        Ok(())
    }

    #[test]
    fn test_parse_program_headers() -> Result<(), Error> {
        // Let's try to parse ourselves!
        // Ideally we'd use std::env::current_exe but that doesn't seem to be implemented (yet?)
        let file = File::open("/pkg/bin/process_builder_lib_test")?;
        let vmo = fdio::get_vmo_copy_from_file(&file)?;

        let headers = Elf64Headers::from_vmo(&vmo)?;
        assert!(headers.program_headers().len() > 0);
        assert!(headers.program_header_with_type(SegmentType::Interp)?.is_some());
        assert!(headers.program_headers_with_type(SegmentType::Dynamic).count() == 1);
        assert!(headers.program_headers_with_type(SegmentType::Load).count() > 1);
        Ok(())
    }

    #[test]
    fn test_parse_static_pie() -> Result<(), Error> {
        // Parse the statically linked PIE test binary.
        let file = File::open("/pkg/bin/static_pie_test_util")?;
        let vmo = fdio::get_vmo_copy_from_file(&file)?;

        // Should have no PT_INTERP header, but should have PT_DYNAMIC and 1+ PT_LOAD.
        let headers = Elf64Headers::from_vmo(&vmo)?;
        assert!(headers.program_headers().len() > 0);
        assert!(headers.program_header_with_type(SegmentType::Interp)?.is_none());
        assert!(headers.program_headers_with_type(SegmentType::Dynamic).count() == 1);
        assert!(headers.program_headers_with_type(SegmentType::Load).count() > 1);
        Ok(())
    }
}
