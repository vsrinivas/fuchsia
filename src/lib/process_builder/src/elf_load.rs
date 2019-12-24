// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

#[derive(Debug)]
pub struct LoadedElf {
    /// The VMAR that the ELF file was loaded into.
    pub vmar: zx::Vmar,

    /// The virtual address of the VMAR.
    pub vmar_base: usize,

    /// The ELF entry point, adjusted for the base address of the VMAR.
    pub entry: usize,
}

pub fn load_elf(
    vmo: &zx::Vmo,
    root_vmar: &zx::Vmar,
    headers: &elf::Elf64Headers,
) -> Result<LoadedElf, ElfLoadError> {
    let elf_vmar = ElfVmar::allocate(root_vmar, headers)?;
    elf_vmar.map_segments(vmo, headers)?;
    Ok(LoadedElf {
        vmar: elf_vmar.vmar,
        vmar_base: elf_vmar.vmar_base,
        entry: headers.file_header().entry.wrapping_add(elf_vmar.vaddr_bias),
    })
}

struct ElfVmar {
    vmar: zx::Vmar,
    vmar_base: usize,

    /// Difference between p_vaddr addresses in the ELF headers and the actual mapped virtual
    /// address within the root VMAR.
    vaddr_bias: usize,
}

impl ElfVmar {
    /// Allocates a new VMAR within the given root VMAR large enough and with appropriate mapping
    /// permissions for the given ELF file. The kernel chooses where the VMAR is located for ASLR.
    fn allocate(root_vmar: &zx::Vmar, headers: &elf::Elf64Headers) -> Result<Self, ElfLoadError> {
        let (mut first, mut low, mut high) = (false, 0, 0);
        let mut max_perm = elf::SegmentFlags::empty();
        for hdr in headers.program_headers_with_type(elf::SegmentType::Load) {
            // elf_parse already checked that segments are ordered by vaddr and do not overlap.
            if first {
                low = util::page_start(hdr.vaddr);
                first = true;
            }
            high = util::page_end(hdr.vaddr + hdr.memsz as usize);
            max_perm |= hdr.flags();
        }

        let size = high - low;
        if size == 0 {
            return Err(ElfLoadError::NothingToLoad);
        }

        // Individual mappings with be restricted based on segment permissions, but we also limit
        // the overall VMAR to the maximum permissions required across all load segments.
        let flags = zx::VmarFlags::CAN_MAP_SPECIFIC | elf_to_vmar_can_map_flags(&max_perm);
        let (vmar, vmar_base) =
            root_vmar.allocate(0, size, flags).map_err(|s| ElfLoadError::VmarAllocate(s))?;

        // We intentionally use wrapping subtraction here, in case the ELF file happens to use
        // vaddr's that are higher than the VMAR base chosen by the kernel. Wrapping addition will
        // be used when adding this bias to vaddr values.
        Ok(ElfVmar { vmar, vmar_base, vaddr_bias: vmar_base.wrapping_sub(low) })
    }

    fn map_segments(&self, vmo: &zx::Vmo, headers: &elf::Elf64Headers) -> Result<(), ElfLoadError> {
        // Get the relative bias between p_vaddr addresses in the headers and the allocated VMAR,
        // rather than for the root VMAR. Should be equal to the first segment's starting vaddr
        // negated, so that the first mapping starts at 0 within the allocated VMAR.
        let rel_bias = self.vaddr_bias.wrapping_sub(self.vmar_base);

        let vmo_name = vmo.get_name().map_err(|s| ElfLoadError::GetVmoName(s))?;
        let mut first = true;
        for hdr in headers.program_headers_with_type(elf::SegmentType::Load) {
            // Sanity check relative bias calculation.
            if first {
                assert!(rel_bias == hdr.vaddr.wrapping_neg());
                first = false;
            }

            // Map in all whole pages that this segment touches. Calculate the virtual address
            // range that this mapping needs to cover. These addresses are relative to the
            // allocated VMAR, not the root VMAR.
            let vaddr_start = hdr.vaddr.wrapping_add(rel_bias);
            let map_start = util::page_start(vaddr_start);
            let map_end = util::page_end(vaddr_start + hdr.memsz as usize);
            let map_size = map_end - map_start;
            if map_size == 0 {
                // Empty segment, ignore and map others.
                continue;
            }

            // Calculate the pages from the VMO that need to be mapped.
            let offset_end = hdr.offset + hdr.filesz as usize;
            let mut vmo_start = util::page_start(hdr.offset);
            let mut vmo_full_page_end = util::page_start(offset_end);
            let vmo_partial_page_size = util::page_offset(offset_end);

            // Page aligned size of VMO content to be mapped in, including any partial pages.
            let vmo_size = util::page_end(offset_end) - vmo_start;
            assert!(map_size >= vmo_size);

            // If this segment is writeable (and we're mapping in some VMO content, i.e. it's not
            // all zero initialized), create a writeable clone of the VMO.
            let vmo_to_map: &zx::Vmo;
            let writeable_vmo: zx::Vmo;
            if vmo_size == 0 || !hdr.flags().contains(elf::SegmentFlags::WRITE) {
                vmo_to_map = vmo;
            } else {
                writeable_vmo = vmo
                    .create_child(
                        zx::VmoChildOptions::COPY_ON_WRITE,
                        vmo_start as u64,
                        vmo_size as u64,
                    )
                    .map_err(ElfLoadError::VmoCowClone)?;
                writeable_vmo
                    .set_name(&vmo_name_with_prefix(&vmo_name, VMO_NAME_PREFIX_DATA))
                    .map_err(ElfLoadError::SetVmoName)?;
                vmo_to_map = &writeable_vmo;

                // Update addresses into the VMO that will be mapped.
                vmo_full_page_end -= vmo_start;
                vmo_start = 0;
            }

            // If the mapping size is equal in size to the data to be mapped, then nothing else to
            // do. Create the mapping and we're done with this segment.
            let flags = zx::VmarFlags::SPECIFIC | elf_to_vmar_perm_flags(&hdr.flags());
            if map_size == vmo_size {
                self.vmar
                    .map(map_start, vmo_to_map, vmo_start as u64, vmo_size, flags)
                    .map_err(ElfLoadError::VmarMap)?;
                continue;
            }

            // Mapping size is larger than the vmo data size (i.e. the segment contains a .bss
            // section). The mapped region beyond the vmo size is zero initialized. We can start
            // out by mapping any full pages from the vmo.
            let vmo_full_page_size = vmo_full_page_end - vmo_start;
            if vmo_full_page_size > 0 {
                self.vmar
                    .map(map_start, vmo_to_map, vmo_start as u64, vmo_full_page_size, flags)
                    .map_err(ElfLoadError::VmarMap)?;
            }

            // Remaining pages are backed by an anonymous VMO, which is automatically zero filled
            // by the kernel as needed.
            let anon_map_start = map_start + vmo_full_page_size;
            let anon_size = map_size - vmo_full_page_size;
            let anon_vmo =
                zx::Vmo::create(anon_size as u64).map_err(|s| ElfLoadError::VmoCreate(s))?;
            anon_vmo
                .set_name(&vmo_name_with_prefix(&vmo_name, VMO_NAME_PREFIX_BSS))
                .map_err(ElfLoadError::SetVmoName)?;

            // If the segment has a partial page of data at the end, it needs to be copied into the
            // anonymous VMO.
            if vmo_partial_page_size > 0 {
                let mut page_buf = [0u8; util::PAGE_SIZE];
                let buf = &mut page_buf[0..vmo_partial_page_size];
                vmo_to_map.read(buf, vmo_full_page_end as u64).map_err(ElfLoadError::VmoRead)?;
                anon_vmo.write(buf, 0).map_err(|s| ElfLoadError::VmoWrite(s))?;
            }

            // Map the anonymous vmo and done with this segment!
            self.vmar
                .map(anon_map_start, &anon_vmo, 0, anon_size, flags)
                .map_err(ElfLoadError::VmarMap)?;
        }
        Ok(())
    }
}

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
    use {super::*, anyhow::Error};

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

    #[test]
    #[should_panic(expected = "MAX_LEN")]
    fn test_vmo_name_with_prefix_too_long() {
        let empty_vmo_name = CStr::from_bytes_with_nul(b"\0").unwrap();
        vmo_name_with_prefix(&empty_vmo_name, b"a_really_long_prefix_that_is_too_long");
    }
}
