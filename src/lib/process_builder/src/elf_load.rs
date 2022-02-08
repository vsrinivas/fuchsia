// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for loading ELF files into an existing address space.

use {
    crate::elf_parse as elf,
    crate::util,
    fuchsia_zircon::{self as zx, AsHandleRef},
    std::ffi::{CStr, CString},
    thiserror::Error,
};

/// Possible errors that can occur during ELF loading.
#[allow(missing_docs)] // No docs on individual error variants.
#[derive(Error, Debug)]
pub enum ElfLoadError {
    #[error("ELF load segments were empty")]
    NothingToLoad,
    #[error("Failed to allocate VMAR for ELF: {}", _0)]
    VmarAllocate(zx::Status),
    #[error("Failed to map VMAR: {}", _0)]
    VmarMap(zx::Status),
    #[error("Failed to create CoW VMO clone: {}", _0)]
    VmoCowClone(zx::Status),
    #[error("Failed to create VMO: {}", _0)]
    VmoCreate(zx::Status),
    #[error("Failed to read from VMO: {}", _0)]
    VmoRead(zx::Status),
    #[error("Failed to write to VMO: {}", _0)]
    VmoWrite(zx::Status),
    #[error("Failed to get VMO name: {}", _0)]
    GetVmoName(zx::Status),
    #[error("Failed to set VMO name: {}", _0)]
    SetVmoName(zx::Status),
}

impl ElfLoadError {
    /// Returns an appropriate zx::Status code for the given error.
    pub fn as_zx_status(&self) -> zx::Status {
        match self {
            ElfLoadError::NothingToLoad => zx::Status::NOT_FOUND,
            ElfLoadError::VmarAllocate(s)
            | ElfLoadError::VmarMap(s)
            | ElfLoadError::VmoCowClone(s)
            | ElfLoadError::VmoCreate(s)
            | ElfLoadError::VmoRead(s)
            | ElfLoadError::VmoWrite(s)
            | ElfLoadError::GetVmoName(s)
            | ElfLoadError::SetVmoName(s) => *s,
        }
    }
}

/// Information on what an ELF requires of its address space.
#[derive(Debug)]
pub struct LoadedElfInfo {
    /// The lowest address of the loaded ELF.
    pub low: usize,

    /// The highest address of the loaded ELF.
    pub high: usize,

    /// Union of all address space permissions required to load the ELF.
    pub max_perm: elf::SegmentFlags,
}

/// Returns the address space requirements to load this ELF. Attempting to load it into a VMAR with
/// permissions less than max_perm, or at a base such that the range [base+low, base+high] is not
/// entirely valid, will fail.
pub fn loaded_elf_info(headers: &elf::Elf64Headers) -> LoadedElfInfo {
    let (mut first, mut low, mut high) = (true, 0, 0);
    let mut max_perm = elf::SegmentFlags::empty();
    for hdr in headers.program_headers_with_type(elf::SegmentType::Load) {
        // elf_parse already checked that segments are ordered by vaddr and do not overlap.
        if first {
            low = util::page_start(hdr.vaddr);
            first = false;
        }
        high = util::page_end(hdr.vaddr + hdr.memsz as usize);
        max_perm |= hdr.flags();
    }
    LoadedElfInfo { low, high, max_perm }
}

/// Return value of load_elf.
#[derive(Debug)]
pub struct LoadedElf {
    /// The VMAR that the ELF file was loaded into.
    pub vmar: zx::Vmar,

    /// The virtual address of the VMAR.
    pub vmar_base: usize,

    /// The ELF entry point, adjusted for the base address of the VMAR.
    pub entry: usize,
}

/// A trait so that callers of map_elf_segments can hook the map operation.
pub trait Mapper {
    /// Map memory from the given VMO at the specified location.
    ///
    /// See zx::Vmar::map for more details.
    fn map(
        &self,
        vmar_offset: usize,
        vmo: &zx::Vmo,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<usize, zx::Status>;
}

impl Mapper for zx::Vmar {
    fn map(
        &self,
        vmar_offset: usize,
        vmo: &zx::Vmo,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<usize, zx::Status> {
        Self::map(self, vmar_offset, vmo, vmo_offset, length, flags)
    }
}

/// Load an ELF into a new sub-VMAR of the specified root.
pub fn load_elf(
    vmo: &zx::Vmo,
    headers: &elf::Elf64Headers,
    root_vmar: &zx::Vmar,
) -> Result<LoadedElf, ElfLoadError> {
    let info = loaded_elf_info(headers);
    let size = info.high - info.low;
    if size == 0 {
        return Err(ElfLoadError::NothingToLoad);
    }

    // Individual mappings with be restricted based on segment permissions, but we also limit the
    // overall VMAR to the maximum permissions required across all load segments.
    let flags = zx::VmarFlags::CAN_MAP_SPECIFIC | elf_to_vmar_can_map_flags(&info.max_perm);
    let (vmar, vmar_base) =
        root_vmar.allocate(0, size, flags).map_err(|s| ElfLoadError::VmarAllocate(s))?;

    // Get the relative bias between p_vaddr addresses in the headers and the allocated VMAR,
    // rather than for the root VMAR. Should be equal to the first segment's starting vaddr
    // negated, so that the first mapping starts at 0 within the allocated VMAR.
    let vaddr_bias = vmar_base.wrapping_sub(info.low);

    map_elf_segments(vmo, headers, &vmar, vmar_base, vaddr_bias)?;
    Ok(LoadedElf { vmar, vmar_base, entry: headers.file_header().entry.wrapping_add(vaddr_bias) })
}

/// Map the segments of an ELF into an existing VMAR.
pub fn map_elf_segments(
    vmo: &zx::Vmo,
    headers: &elf::Elf64Headers,
    mapper: &dyn Mapper,
    mapper_base: usize,
    vaddr_bias: usize,
) -> Result<(), ElfLoadError> {
    // We intentionally use wrapping subtraction here, in case the ELF file happens to use vaddr's
    // that are higher than the VMAR base chosen by the kernel. Wrapping addition will be used when
    // adding this bias to vaddr values.
    let mapper_relative_bias = vaddr_bias.wrapping_sub(mapper_base);
    let vmo_name = vmo.get_name().map_err(|s| ElfLoadError::GetVmoName(s))?;
    for hdr in headers.program_headers_with_type(elf::SegmentType::Load) {
        // Shift the start of the mapping down to the nearest page.
        let adjust = util::page_offset(hdr.offset);
        let mut file_offset = hdr.offset - adjust;
        let file_size = hdr.filesz + adjust as u64;
        let virt_offset = hdr.vaddr - adjust;
        let virt_size = hdr.memsz + adjust as u64;

        // Calculate the virtual address range that this mapping needs to cover. These addresses
        // are relative to the allocated VMAR, not the root VMAR.
        let virt_addr = virt_offset.wrapping_add(mapper_relative_bias);

        // If the segment is specified as larger than the data in the file, and the data in the file
        // does not end at a page boundary, we will need to zero out the remaining memory in the
        // page.
        let must_write = virt_size > file_size && util::page_offset(file_size as usize) != 0;

        // If this segment is writeable (and we're mapping in some VMO content, i.e. it's not
        // all zero initialized) or the segment has a BSS section that needs to be zeroed, create
        // a writeable clone of the VMO. Otherwise use the potentially read-only VMO passed in.
        let vmo_to_map: &zx::Vmo;
        let writeable_vmo: zx::Vmo;
        if must_write || (file_size > 0 && hdr.flags().contains(elf::SegmentFlags::WRITE)) {
            writeable_vmo = vmo
                .create_child(
                    zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE,
                    file_offset as u64,
                    util::page_end(file_size as usize) as u64,
                )
                .map_err(ElfLoadError::VmoCowClone)?;
            writeable_vmo
                .set_name(&vmo_name_with_prefix(&vmo_name, VMO_NAME_PREFIX_DATA))
                .map_err(ElfLoadError::SetVmoName)?;
            // Update addresses into the VMO that will be mapped.
            file_offset = 0;

            // Zero-out the memory between the end of the filesize and the end of the page.
            if virt_size > file_size {
                // If the space to be zero-filled overlaps with the VMO, we need to memset it.
                let memset_size = util::page_end(file_size as usize) - file_size as usize;
                if memset_size > 0 {
                    writeable_vmo
                        .write(&vec![0u8; memset_size], file_size)
                        .map_err(|s| ElfLoadError::VmoWrite(s))?;
                }
            }
            vmo_to_map = &writeable_vmo;
        } else {
            vmo_to_map = vmo;
        }

        // Create the VMO part of the mapping.
        let flags = zx::VmarFlags::SPECIFIC | elf_to_vmar_perm_flags(&hdr.flags());
        if file_size != 0 {
            mapper
                .map(
                    virt_addr,
                    vmo_to_map,
                    file_offset as u64,
                    util::page_end(file_size as usize),
                    flags,
                )
                .map_err(ElfLoadError::VmarMap)?;
        }

        // If the mapping is specified as larger than the data in the file (i.e. virt_size is
        // larger than file_size), the remainder of the space (from virt_addr + file_size to
        // virt_addr + virt_size) is the BSS and must be filled with zeros.
        if virt_size > file_size {
            // The rest of the BSS is created as an anonymous vmo.
            let bss_vmo_start = util::page_end(file_size as usize);
            let bss_vmo_size = util::page_end(virt_size as usize) - bss_vmo_start;
            if bss_vmo_size > 0 {
                let anon_vmo =
                    zx::Vmo::create(bss_vmo_size as u64).map_err(|s| ElfLoadError::VmoCreate(s))?;
                anon_vmo
                    .set_name(&vmo_name_with_prefix(&vmo_name, VMO_NAME_PREFIX_BSS))
                    .map_err(ElfLoadError::SetVmoName)?;
                mapper
                    .map(virt_addr + bss_vmo_start, &anon_vmo, 0, bss_vmo_size, flags)
                    .map_err(ElfLoadError::VmarMap)?;
            }
        }
    }
    Ok(())
}

// These must not be longer than zx::sys::ZX_MAX_NAME_LEN.
const VMO_NAME_UNKNOWN: &[u8] = b"<unknown ELF>";
const VMO_NAME_PREFIX_BSS: &[u8] = b"bss:";
const VMO_NAME_PREFIX_DATA: &[u8] = b"data:";

// prefix length must be less than zx::sys::ZX_MAX_NAME_LEN-1 and not contain any nul bytes.
fn vmo_name_with_prefix(name: &CStr, prefix: &[u8]) -> CString {
    const MAX_LEN: usize = zx::sys::ZX_MAX_NAME_LEN - 1;
    assert!(prefix.len() <= MAX_LEN);

    let mut name_bytes = name.to_bytes();
    if name_bytes.len() == 0 {
        name_bytes = VMO_NAME_UNKNOWN;
    }
    let name_len = std::cmp::min(MAX_LEN, prefix.len() + name_bytes.len());
    let suffix_len = name_len - prefix.len();

    let mut buf = Vec::with_capacity(name_len);
    buf.extend_from_slice(prefix);
    buf.extend_from_slice(&name_bytes[..suffix_len]);
    assert!(buf.len() <= MAX_LEN);

    // The input name is already a CStr, so it doesn't contain nul, so this should only fail if the
    // prefix contains a nul, and since the prefixes are constants, panic if this fails.
    CString::new(buf).expect("Unexpected nul byte in prefix")
}

fn elf_to_vmar_can_map_flags(elf_flags: &elf::SegmentFlags) -> zx::VmarFlags {
    let mut flags = zx::VmarFlags::empty();
    if elf_flags.contains(elf::SegmentFlags::READ) {
        flags |= zx::VmarFlags::CAN_MAP_READ;
    }
    if elf_flags.contains(elf::SegmentFlags::WRITE) {
        flags |= zx::VmarFlags::CAN_MAP_WRITE;
    }
    if elf_flags.contains(elf::SegmentFlags::EXECUTE) {
        flags |= zx::VmarFlags::CAN_MAP_EXECUTE;
    }
    flags
}

fn elf_to_vmar_perm_flags(elf_flags: &elf::SegmentFlags) -> zx::VmarFlags {
    let mut flags = zx::VmarFlags::empty();
    if elf_flags.contains(elf::SegmentFlags::READ) {
        flags |= zx::VmarFlags::PERM_READ;
    }
    if elf_flags.contains(elf::SegmentFlags::WRITE) {
        flags |= zx::VmarFlags::PERM_WRITE;
    }
    if elf_flags.contains(elf::SegmentFlags::EXECUTE) {
        flags |= zx::VmarFlags::PERM_EXECUTE;
    }
    flags
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::elf_parse, anyhow::Error, assert_matches::assert_matches,
        fidl::HandleBased, std::cell::RefCell, std::mem::size_of,
    };

    #[test]
    fn test_vmo_name_with_prefix() -> Result<(), Error> {
        let empty_vmo_name = CStr::from_bytes_with_nul(b"\0")?;
        let short_vmo_name = CStr::from_bytes_with_nul(b"short_vmo_name\0")?;
        let max_vmo_name = CStr::from_bytes_with_nul(b"a_great_maximum_length_vmo_name\0")?;

        assert_eq!(
            vmo_name_with_prefix(&empty_vmo_name, VMO_NAME_PREFIX_BSS).as_bytes(),
            b"bss:<unknown ELF>"
        );
        assert_eq!(
            vmo_name_with_prefix(&short_vmo_name, VMO_NAME_PREFIX_BSS).as_bytes(),
            b"bss:short_vmo_name"
        );
        assert_eq!(
            vmo_name_with_prefix(&max_vmo_name, VMO_NAME_PREFIX_BSS).as_bytes(),
            b"bss:a_great_maximum_length_vmo_"
        );
        assert_eq!(
            vmo_name_with_prefix(&max_vmo_name, VMO_NAME_PREFIX_DATA).as_bytes(),
            b"data:a_great_maximum_length_vmo"
        );

        assert_eq!(
            vmo_name_with_prefix(&empty_vmo_name, b"a_long_vmo_name_prefix:").as_bytes(),
            b"a_long_vmo_name_prefix:<unknown"
        );
        assert_eq!(
            vmo_name_with_prefix(&empty_vmo_name, max_vmo_name.to_bytes()).as_bytes(),
            max_vmo_name.to_bytes()
        );
        assert_eq!(
            vmo_name_with_prefix(&max_vmo_name, max_vmo_name.to_bytes()).as_bytes(),
            max_vmo_name.to_bytes()
        );
        Ok(())
    }

    #[derive(Debug)]
    struct RecordedMapping {
        vmo: zx::Vmo,
        vmo_offset: u64,
        length: usize,
    }

    /// Records which VMOs and the offset within them are to be mapped.
    struct TrackingMapper(RefCell<Vec<RecordedMapping>>);

    impl TrackingMapper {
        fn new() -> Self {
            Self(RefCell::new(Vec::new()))
        }
    }

    impl IntoIterator for TrackingMapper {
        type Item = RecordedMapping;
        type IntoIter = std::vec::IntoIter<Self::Item>;

        fn into_iter(self) -> Self::IntoIter {
            self.0.into_inner().into_iter()
        }
    }

    impl Mapper for TrackingMapper {
        fn map(
            &self,
            vmar_offset: usize,
            vmo: &zx::Vmo,
            vmo_offset: u64,
            length: usize,
            _flags: zx::VmarFlags,
        ) -> Result<usize, zx::Status> {
            self.0.borrow_mut().push(RecordedMapping {
                vmo: vmo.as_handle_ref().duplicate(zx::Rights::SAME_RIGHTS).unwrap().into(),
                vmo_offset,
                length,
            });
            Ok(vmar_offset)
        }
    }

    /// A basic ELF64 File header with one program header.
    const ELF_FILE_HEADER: &elf_parse::Elf64FileHeader = &elf_parse::Elf64FileHeader {
        ident: elf_parse::ElfIdent {
            magic: elf_parse::ELF_MAGIC,
            class: elf_parse::ElfClass::Elf64 as u8,
            data: elf_parse::NATIVE_ENCODING as u8,
            version: elf_parse::ElfVersion::Current as u8,
            osabi: 0x00,
            abiversion: 0x00,
            pad: [0; 7],
        },
        elf_type: elf_parse::ElfType::SharedObject as u16,
        machine: elf_parse::CURRENT_ARCH as u16,
        version: elf_parse::ElfVersion::Current as u32,
        entry: 0x10000,
        phoff: size_of::<elf_parse::Elf64FileHeader>(),
        shoff: 0,
        flags: 0,
        ehsize: size_of::<elf_parse::Elf64FileHeader>() as u16,
        phentsize: size_of::<elf_parse::Elf64ProgramHeader>() as u16,
        phnum: 1,
        shentsize: 0,
        shnum: 0,
        shstrndx: 0,
    };

    // The bitwise `|` operator for `bitflags` is implemented through the `std::ops::BitOr` trait,
    // which cannot be used in a const context. The workaround is to bitwise OR the raw bits.
    const VMO_DEFAULT_RIGHTS: zx::Rights = zx::Rights::from_bits_truncate(
        zx::Rights::DUPLICATE.bits()
            | zx::Rights::TRANSFER.bits()
            | zx::Rights::READ.bits()
            | zx::Rights::WRITE.bits()
            | zx::Rights::MAP.bits()
            | zx::Rights::GET_PROPERTY.bits()
            | zx::Rights::SET_PROPERTY.bits(),
    );

    #[test]
    fn map_read_only_with_page_unaligned_bss() {
        const ELF_DATA: &[u8; 8] = b"FUCHSIA!";

        /// Contains a PT_LOAD segment where the filesz is less than memsz (BSS).
        const ELF_PROGRAM_HEADERS: &[elf_parse::Elf64ProgramHeader] =
            &[elf_parse::Elf64ProgramHeader {
                segment_type: elf_parse::SegmentType::Load as u32,
                flags: elf_parse::SegmentFlags::from_bits_truncate(
                    elf_parse::SegmentFlags::READ.bits() | elf_parse::SegmentFlags::EXECUTE.bits(),
                )
                .bits(),
                offset: util::PAGE_SIZE,
                vaddr: 0x10000,
                paddr: 0x10000,
                filesz: ELF_DATA.len() as u64,
                memsz: 0x100,
                align: util::PAGE_SIZE as u64,
            }];

        let headers =
            elf_parse::Elf64Headers::new_for_test(ELF_FILE_HEADER, Some(ELF_PROGRAM_HEADERS));
        let vmo = zx::Vmo::create(util::PAGE_SIZE as u64 * 2).expect("create VMO");

        // Fill the VMO with 0xff, so that we can verify that the BSS section is correctly zeroed.
        vmo.write(&[0xff; util::PAGE_SIZE * 2], 0).expect("fill VMO with 0xff");
        // Write the PT_LOAD segment's data at the defined offset.
        vmo.write(ELF_DATA, util::PAGE_SIZE as u64).expect("write data to VMO");

        // Remove the ZX_RIGHT_WRITE right. Page zeroing should happen in a COW VMO.
        let vmo =
            vmo.replace_handle(VMO_DEFAULT_RIGHTS - zx::Rights::WRITE).expect("remove WRITE right");

        let mapper = TrackingMapper::new();
        map_elf_segments(&vmo, &headers, &mapper, 0, 0).expect("map ELF segments");

        let mut mapping_iter = mapper.into_iter();

        // Extract the VMO and offset that was supposed to be mapped.
        let mapping = mapping_iter.next().expect("mapping from ELF VMO");

        // Read a page of data that was "mapped".
        let mut data = [0; util::PAGE_SIZE];
        mapping.vmo.read(&mut data, mapping.vmo_offset).expect("read VMO");

        // Construct the expected memory, which is ASCII "FUCHSIA!" followed by 0s for the rest of
        // the page.
        let expected = ELF_DATA
            .into_iter()
            .cloned()
            .chain(std::iter::repeat(0).take(util::PAGE_SIZE - ELF_DATA.len()))
            .collect::<Vec<u8>>();

        assert_eq!(&expected, &data);

        // No more mappings expected.
        assert_matches!(mapping_iter.next(), None);
    }

    #[test]
    fn map_read_only_vmo_with_page_aligned_bss() {
        // Contains a PT_LOAD segment where the BSS starts at a page boundary.
        const ELF_PROGRAM_HEADERS: &[elf_parse::Elf64ProgramHeader] =
            &[elf_parse::Elf64ProgramHeader {
                segment_type: elf_parse::SegmentType::Load as u32,
                flags: elf_parse::SegmentFlags::from_bits_truncate(
                    elf_parse::SegmentFlags::READ.bits() | elf_parse::SegmentFlags::EXECUTE.bits(),
                )
                .bits(),
                offset: util::PAGE_SIZE,
                vaddr: 0x10000,
                paddr: 0x10000,
                filesz: util::PAGE_SIZE as u64,
                memsz: util::PAGE_SIZE as u64 * 2,
                align: util::PAGE_SIZE as u64,
            }];
        let headers =
            elf_parse::Elf64Headers::new_for_test(ELF_FILE_HEADER, Some(ELF_PROGRAM_HEADERS));
        let vmo = zx::Vmo::create(util::PAGE_SIZE as u64 * 2).expect("create VMO");
        // Fill the VMO with 0xff, so we can verify the BSS section is correctly allocated.
        vmo.write(&[0xff; util::PAGE_SIZE * 2], 0).expect("fill VMO with 0xff");

        // Remove the ZX_RIGHT_WRITE right. Since the BSS ends at a page boundary, we shouldn't
        // need to zero out any of the pages in this VMO.
        let vmo =
            vmo.replace_handle(VMO_DEFAULT_RIGHTS - zx::Rights::WRITE).expect("remove WRITE right");

        let mapper = TrackingMapper::new();
        map_elf_segments(&vmo, &headers, &mapper, 0, 0).expect("map ELF segments");

        let mut mapping_iter = mapper.into_iter();

        // Verify that a COW VMO was not created, since we didn't need to write to the original VMO.
        // We must check that KOIDs are the same, since we duplicate the handle when recording it
        // in TrackingMapper.
        let mapping = mapping_iter.next().expect("mapping from ELF VMO");
        assert_eq!(mapping.vmo.get_koid().unwrap(), vmo.get_koid().unwrap());

        let mut data = [0u8; util::PAGE_SIZE];

        // Ensure the first page is from the ELF.
        mapping.vmo.read(&mut data, mapping.vmo_offset).expect("read ELF VMO");
        assert_eq!(&data, &[0xffu8; util::PAGE_SIZE]);

        let mapping = mapping_iter.next().expect("mapping from BSS VMO");

        // Ensure the second page is BSS.
        mapping.vmo.read(&mut data, mapping.vmo_offset).expect("read BSS VMO");
        assert_eq!(&data, &[0u8; util::PAGE_SIZE]);

        // No more mappings expected.
        assert_matches!(mapping_iter.next(), None);
    }

    #[test]
    fn map_read_only_vmo_with_no_bss() {
        // Contains a PT_LOAD segment where there is no BSS.
        const ELF_PROGRAM_HEADERS: &[elf_parse::Elf64ProgramHeader] =
            &[elf_parse::Elf64ProgramHeader {
                segment_type: elf_parse::SegmentType::Load as u32,
                flags: elf_parse::SegmentFlags::from_bits_truncate(
                    elf_parse::SegmentFlags::READ.bits() | elf_parse::SegmentFlags::EXECUTE.bits(),
                )
                .bits(),
                offset: util::PAGE_SIZE,
                vaddr: 0x10000,
                paddr: 0x10000,
                filesz: util::PAGE_SIZE as u64,
                memsz: util::PAGE_SIZE as u64,
                align: util::PAGE_SIZE as u64,
            }];
        let headers =
            elf_parse::Elf64Headers::new_for_test(ELF_FILE_HEADER, Some(ELF_PROGRAM_HEADERS));
        let vmo = zx::Vmo::create(util::PAGE_SIZE as u64 * 2).expect("create VMO");
        // Fill the VMO with 0xff, so we can verify the BSS section is correctly allocated.
        vmo.write(&[0xff; util::PAGE_SIZE * 2], 0).expect("fill VMO with 0xff");

        // Remove the ZX_RIGHT_WRITE right. Since the BSS ends at a page boundary, we shouldn't
        // need to zero out any of the pages in this VMO.
        let vmo =
            vmo.replace_handle(VMO_DEFAULT_RIGHTS - zx::Rights::WRITE).expect("remove WRITE right");

        let mapper = TrackingMapper::new();
        map_elf_segments(&vmo, &headers, &mapper, 0, 0).expect("map ELF segments");

        let mut mapping_iter = mapper.into_iter();

        // Verify that a COW VMO was not created, since we didn't need to write to the original VMO.
        // We must check that KOIDs are the same, since we duplicate the handle when recording it
        // in TrackingMapper.
        let mapping = mapping_iter.next().expect("mapping from ELF VMO");
        assert_eq!(mapping.vmo.get_koid().unwrap(), vmo.get_koid().unwrap());

        let mut data = [0u8; util::PAGE_SIZE];

        // Ensure the first page is from the ELF.
        mapping.vmo.read(&mut data, mapping.vmo_offset).expect("read ELF VMO");
        assert_eq!(&data, &[0xffu8; util::PAGE_SIZE]);

        // No more mappings expected.
        assert_matches!(mapping_iter.next(), None);
    }

    #[test]
    fn map_read_only_vmo_with_write_flag() {
        // Contains a PT_LOAD segment where there is no BSS.
        const ELF_PROGRAM_HEADERS: &[elf_parse::Elf64ProgramHeader] =
            &[elf_parse::Elf64ProgramHeader {
                segment_type: elf_parse::SegmentType::Load as u32,
                flags: elf_parse::SegmentFlags::from_bits_truncate(
                    elf_parse::SegmentFlags::READ.bits() | elf_parse::SegmentFlags::WRITE.bits(),
                )
                .bits(),
                offset: util::PAGE_SIZE,
                vaddr: 0x10000,
                paddr: 0x10000,
                filesz: util::PAGE_SIZE as u64,
                memsz: util::PAGE_SIZE as u64,
                align: util::PAGE_SIZE as u64,
            }];
        let headers =
            elf_parse::Elf64Headers::new_for_test(ELF_FILE_HEADER, Some(ELF_PROGRAM_HEADERS));
        let vmo = zx::Vmo::create(util::PAGE_SIZE as u64 * 2).expect("create VMO");

        // Remove the ZX_RIGHT_WRITE right. Since the segment has a WRITE flag, a COW child VMO
        // will be created.
        let vmo =
            vmo.replace_handle(VMO_DEFAULT_RIGHTS - zx::Rights::WRITE).expect("remove WRITE right");

        let mapper = TrackingMapper::new();
        map_elf_segments(&vmo, &headers, &mapper, 0, 0).expect("map ELF segments");

        let mut mapping_iter = mapper.into_iter();

        // Verify that a COW VMO was created, since the segment had a WRITE flag.
        // We must check that KOIDs are different, since we duplicate the handle when recording it
        // in TrackingMapper.
        let mapping = mapping_iter.next().expect("mapping from ELF VMO");
        assert_ne!(mapping.vmo.get_koid().unwrap(), vmo.get_koid().unwrap());

        // Attempt to write to the VMO to ensure it has the ZX_RIGHT_WRITE right.
        mapping.vmo.write(b"FUCHSIA!", mapping.vmo_offset).expect("write to COW VMO");

        // No more mappings expected.
        assert_matches!(mapping_iter.next(), None);
    }

    #[test]
    fn segment_with_zero_file_size() {
        // Contains a PT_LOAD segment whose filesz is 0.
        const ELF_PROGRAM_HEADERS: &[elf_parse::Elf64ProgramHeader] =
            &[elf_parse::Elf64ProgramHeader {
                segment_type: elf_parse::SegmentType::Load as u32,
                flags: elf_parse::SegmentFlags::from_bits_truncate(
                    elf_parse::SegmentFlags::READ.bits() | elf_parse::SegmentFlags::WRITE.bits(),
                )
                .bits(),
                offset: util::PAGE_SIZE,
                vaddr: 0x10000,
                paddr: 0x10000,
                filesz: 0,
                memsz: 1,
                align: util::PAGE_SIZE as u64,
            }];
        let headers =
            elf_parse::Elf64Headers::new_for_test(ELF_FILE_HEADER, Some(ELF_PROGRAM_HEADERS));
        let vmo = zx::Vmo::create(util::PAGE_SIZE as u64 * 2).expect("create VMO");

        let mapper = TrackingMapper::new();
        map_elf_segments(&vmo, &headers, &mapper, 0, 0).expect("map ELF segments");
        for mapping in mapper.into_iter() {
            assert!(mapping.length != 0);
        }
    }
}
