// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Allows extracting DSO contextual information in-process within Fuchsia processes
//! that use the standard Fuchsia libc.

#![deny(missing_docs)]

use {
    bitflags::bitflags,
    std::ffi::CStr,
    std::fmt,
    std::mem::{size_of, transmute},
    std::os::raw::c_char,
    std::slice::from_raw_parts,
};

extern "C" {
    // dl_iterate_phdr takes a callback that will receive a dl_phdr_info pointer
    // for every DSO that has been linked into the process. dl_iterate_phdr also
    // ensures that the dynamic linker is locked from start to finish of the
    // iteration. If the callback returns a non-zero value the iteration is
    // terminated early. 'data' will be passed as the third argument to the
    // callback on each call. 'size' gives the size of the dl_phdr_info.
    #[allow(improper_ctypes)]
    fn dl_iterate_phdr(
        f: extern "C" fn(info: &dl_phdr_info, size: usize, data: &mut &mut dyn DsoVisitor) -> i32,
        data: &mut &mut dyn DsoVisitor,
    ) -> i32;
}

// We need to parse out the build ID and some basic program header data
// which means that we need a bit of stuff from the ELF spec as well.

const PT_LOAD: u32 = 1;
const PT_NOTE: u32 = 4;

// Now we have to replicate, bit for bit, the structure of the dl_phdr_info
// type used by fuchsia's current dynamic linker. Chromium also has this ABI
// boundary as well as crashpad. Eventully we'd like to move these cases to
// use elf-search but we'd need to provide that in the SDK and that has not
// yet been done. Thus we (and they) are stuck having to use this method
// which incurs a tight coupling with the fuchsia libc.

#[allow(non_camel_case_types)]
#[repr(C)]
struct dl_phdr_info {
    addr: *const u8,
    name: *const c_char,
    phdr: *const Elf_Phdr,
    phnum: u16,
    adds: u64,
    subs: u64,
    tls_modid: usize,
    tls_data: *const u8,
}

impl dl_phdr_info {
    fn program_headers(&self) -> PhdrIter<'_> {
        PhdrIter { phdrs: self.phdr_slice(), base: self.addr }
    }
    // We have no way of knowing of checking if e_phoff and e_phnum are valid.
    // libc should ensure this for us however so it's safe to form a slice here.
    fn phdr_slice(&self) -> &[Elf_Phdr] {
        unsafe { from_raw_parts(self.phdr, self.phnum as usize) }
    }
}

struct PhdrIter<'a> {
    phdrs: &'a [Elf_Phdr],
    base: *const u8,
}

impl<'a> Iterator for PhdrIter<'a> {
    type Item = Phdr<'a>;
    fn next(&mut self) -> Option<Self::Item> {
        self.phdrs.split_first().map(|(phdr, new_phdrs)| {
            self.phdrs = new_phdrs;
            Phdr { phdr, base: self.base }
        })
    }
}

// Elf_Phdr represents a 64-bit ELF program header in the endianness of the target
// architecture.
#[allow(non_camel_case_types)]
#[derive(Clone, Debug)]
#[repr(C)]
struct Elf_Phdr {
    p_type: u32,
    p_flags: u32,
    p_offset: u64,
    p_vaddr: u64,
    p_paddr: u64,
    p_filesz: u64,
    p_memsz: u64,
    p_align: u64,
}

// Phdr represents a valid ELF program header and its contents.
struct Phdr<'a> {
    phdr: &'a Elf_Phdr,
    base: *const u8,
}

impl<'a> Phdr<'a> {
    // We have no way of checking if p_addr or p_memsz are valid. Fuchsia's libc
    // parses the notes first however so by virtue of being here these headers
    // must be valid. NoteIter does not require the underlying data to be valid
    // but it does require the bounds to be valid. We trust that libc has ensured
    // that this is the case for us here.
    fn notes(&self) -> NoteIter<'a> {
        unsafe {
            NoteIter::new(self.base.add(self.phdr.p_offset as usize), self.phdr.p_memsz as usize)
        }
    }
}

// The note type for build IDs.
const NT_GNU_BUILD_ID: u32 = 3;

// Elf_Nhdr represents an ELF note header in the endianness of the target.
#[allow(non_camel_case_types)]
#[repr(C)]
struct Elf_Nhdr {
    n_namesz: u32,
    n_descsz: u32,
    n_type: u32,
}

// Note represents an ELF note (header + contents). The name is left as a u8
// slice because it is not always null terminated and rust makes it easy enough
// to check that the bytes match eitherway.
struct Note<'a> {
    name: &'a [u8],
    desc: &'a [u8],
    tipe: u32,
}

// NoteIter lets you safely iterate over a note segment. It terminates as soon
// as an error occurs or there are no more notes. If you iterate over invalid
// data it will function as though no notes were found.
struct NoteIter<'a> {
    base: &'a [u8],
    error: bool,
}

impl<'a> NoteIter<'a> {
    // It is an invariant of function that the pointer and size given denote a
    // valid range of bytes that can all be read. The contents of these bytes
    // can be anything but the range must be valid for this to be safe.
    unsafe fn new(base: *const u8, size: usize) -> Self {
        NoteIter { base: from_raw_parts(base, size), error: false }
    }
}

// align_to aligns 'x' to 'to'-byte alignment assuming 'to' is a power of 2.
// This follows a standard pattern in C/C++ ELF parsing code where
// (x + to - 1) & -to is used. Rust does not let you negate usize so I use
// 2's-complement conversion to recreate that.
fn align_to(x: usize, to: usize) -> usize {
    (x + to - 1) & (!to + 1)
}

// take_bytes_align4 consumes num bytes from the slice (if present) and
// additionally ensures that the final slice is properlly aligned. If an
// either the number of bytes requested is too large or the slice can't be
// realigned afterwards due to not enough remaining bytes existing, None is
// returned and the slice is not modified.
fn take_bytes_align4<'a>(num: usize, bytes: &mut &'a [u8]) -> Option<&'a [u8]> {
    if bytes.len() < align_to(num, 4) {
        return None;
    }
    let (out, bytes_new) = bytes.split_at(num);
    *bytes = &bytes_new[align_to(num, 4) - num..];
    Some(out)
}

// This function has no real invariants the caller must uphold other than
// perhaps that 'bytes' should be aligned for performance (and on some
// architectures correctness). The values in the Elf_Nhdr fields might
// be nonsense but this function ensures no such thing.
fn take_nhdr<'a>(bytes: &mut &'a [u8]) -> Option<&'a Elf_Nhdr> {
    if size_of::<Elf_Nhdr>() > bytes.len() {
        return None;
    }
    // This is safe as long as there is enough space and we just confirmed that
    // in the if statement above so this should not be unsafe.
    let out = unsafe { transmute::<*const u8, &'a Elf_Nhdr>(bytes.as_ptr()) };
    // Note that sice_of::<Elf_Nhdr>() is always 4-byte aligned.
    *bytes = &bytes[size_of::<Elf_Nhdr>()..];
    Some(out)
}

impl<'a> Iterator for NoteIter<'a> {
    type Item = Note<'a>;
    fn next(&mut self) -> Option<Self::Item> {
        // Check if we've reached the end.
        if self.base.len() == 0 || self.error {
            return None;
        }
        // We transmute out an nhdr but we carefully consider the resulting
        // struct. We don't trust the namesz or descsz and we make no unsafe
        // decisions based on the type. So even if we get out complete garbage
        // we should still be safe.
        let nhdr = take_nhdr(&mut self.base)?;
        let name = take_bytes_align4(nhdr.n_namesz as usize, &mut self.base)?;
        let desc = take_bytes_align4(nhdr.n_descsz as usize, &mut self.base)?;
        Some(Note { name: name, desc: desc, tipe: nhdr.n_type })
    }
}

// This section defines the public interface of this library.

bitflags! {
    /// Represents memory permissions for an ELF segment.
    pub struct Perm: u32 {
        /// Indicates that a segment is executable.
        const X = 0b00000001;
        /// Indicates that a segment is writable.
        const W = 0b00000010;
        /// Indicates that a segment is readable.
        const R = 0b00000100;
    }
}

impl fmt::Display for Perm {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        if *self & Perm::R == Perm::R {
            write!(f, "r")?
        }
        if *self & Perm::W == Perm::W {
            write!(f, "w")?
        }
        if *self & Perm::X == Perm::X {
            write!(f, "x")?
        }
        Ok(())
    }
}

/// Represents an ELF segment at runtime.
#[derive(Copy, Clone, Debug)]
pub struct Segment {
    /// Gives the runtime virtual address of this segment's contents.
    pub addr: usize,
    /// Gives the memory size of this segment's contents.
    pub size: usize,
    /// Gives the module virtual address of this segment with the ELF file.
    pub mod_rel_addr: usize,
    /// Gives the permissions found in the ELF file. These permissions are not
    /// necessarily the permissions present at runtime however.
    pub flags: Perm,
}

/// Lets one iterate over Segments from a DSO.
#[derive(Copy, Clone, Debug)]
pub struct SegmentIter<'a> {
    phdrs: &'a [Elf_Phdr],
    base: usize,
}

impl Iterator for SegmentIter<'_> {
    type Item = Segment;

    fn next(&mut self) -> Option<Self::Item> {
        self.phdrs.split_first().and_then(|(phdr, new_phdrs)| {
            self.phdrs = new_phdrs;
            if phdr.p_type != PT_LOAD {
                self.next()
            } else {
                Some(Segment {
                    addr: phdr.p_vaddr as usize + self.base,
                    size: phdr.p_memsz as usize,
                    mod_rel_addr: phdr.p_vaddr as usize,
                    flags: Perm::from_bits_truncate(phdr.p_flags),
                })
            }
        })
    }
}

/// Represents an ELF DSO (Dynamic Shared Object). This type references
/// the data stored in the actual DSO rather than making its own copy.
#[derive(Copy, Clone, Debug)]
pub struct Dso<'a> {
    /// The dynamic linker always gives us a name, even if the name is empty.
    /// In the case of the main executable this name will be empty. In the case
    /// of a shared object it will be the soname (see DT_SONAME).
    pub name: &'a str,
    /// On Fuchsia virtually all binaries have build IDs but this is not a strict
    /// requierment. There's no way to match up DSO information with a real ELF
    /// file afterwards if there is no build_id so we require that every DSO
    /// have one here. DSO's without a build_id are ignored.
    pub build_id: &'a [u8],

    base: usize,
    phdrs: &'a [Elf_Phdr],
}

impl Dso<'_> {
    /// Returns an iterator over Segments in this DSO.
    pub fn segments(&self) -> SegmentIter<'_> {
        SegmentIter { phdrs: self.phdrs.as_ref(), base: self.base }
    }
}

/// Represents an ELF DSO (Dynamic Shared Object). This type owns
/// its data but requires a copy/allocation in general.
pub struct OwnedDso {
    /// The dynamic linker always gives us a name, even if the name is empty.
    /// In the case of the main executable this name will be empty. In the case
    /// of a shared object it will be the soname (see DT_SONAME).
    pub name: String,
    /// On Fuchsia virtually all binaries have build IDs but this is not a strict
    /// requierment. There's no way to match up DSO information with a real ELF
    /// file afterwards if there is no build_id so we require that every DSO
    /// have one here. DSO's without a build_id are ignored.
    pub build_id: Vec<u8>,

    base: usize,
    phdrs: Vec<Elf_Phdr>,
}

impl OwnedDso {
    /// Returns an iterator over Segments in this DSO.
    ///
    /// # Arguments
    ///
    /// * `self` - An OwnedDso.
    ///
    /// # Example
    /// ```
    /// for seg in dso.segments() {
    ///   handle_segment(seg);
    /// }
    /// ```
    pub fn segments(&self) -> SegmentIter<'_> {
        return SegmentIter { phdrs: self.phdrs.as_ref(), base: self.base };
    }
}

impl<'a> From<Dso<'a>> for OwnedDso {
    fn from(dso: Dso<'a>) -> Self {
        OwnedDso {
            name: dso.name.to_string(),
            build_id: dso.build_id.to_vec(),
            base: dso.base,
            phdrs: dso.phdrs.to_vec(),
        }
    }
}

struct HexSlice<'a> {
    bytes: &'a [u8],
}

impl fmt::Display for HexSlice<'_> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        for byte in self.bytes {
            write!(f, "{:x}", byte)?;
        }
        Ok(())
    }
}

fn get_build_id<'a>(info: &'a dl_phdr_info) -> Option<&'a [u8]> {
    for phdr in info.program_headers() {
        if phdr.phdr.p_type == PT_NOTE {
            for note in phdr.notes() {
                if note.tipe == NT_GNU_BUILD_ID && (note.name == b"GNU\0" || note.name == b"GNU") {
                    return Some(note.desc);
                }
            }
        }
    }
    None
}

/// These errors encode issues that arise while parsing information about
/// each DSO.
pub enum Error {
    /// NameError means that an error occurred while converting a C style string
    /// into a rust string.
    NameError(std::str::Utf8Error),
    /// BuildIDError means that we didn't find a build ID. This could either be
    /// because the DSO had no build ID or because the segment containing the
    /// build ID was malformed.
    BuildIDError,
}

/// DsoVisitor handles the two cases that can arise during iteration. Either an
/// error while parsing can occur, or you might find a valid DSO.
pub trait DsoVisitor {
    /// This is called when a valid DSO is found with a build ID.
    fn dso(&mut self, dso: Dso<'_>) -> bool;
    /// This is called anytime an error occurs when we think we've found a DSO
    /// but something else went wrong and we couldn't deliver a fully valid DSO.
    fn error(&mut self, error: Error) -> bool;
}

/// Calls either 'dso' or 'error' for each DSO linked into the process by the
/// dynamic linker.
///
/// # Arguments
///
/// * `visitor` - A DsoVisitor that will have one of eats methods called foreach DSO.
///
/// # Example
/// ```
/// let mut my_visitor = MyVisitor{...};
/// for_each_dso(&mut my_visitor);
/// ```
pub fn for_each_dso(mut visitor: &mut dyn DsoVisitor) {
    extern "C" fn callback(
        info: &dl_phdr_info,
        _size: usize,
        visitor: &mut &mut dyn DsoVisitor,
    ) -> i32 {
        // dl_iterate_phdr ensures that info.name will point to a valid
        // location.
        let name = match unsafe { CStr::from_ptr(info.name).to_str() } {
            Ok(name) => name,
            Err(err) => {
                return visitor.error(Error::NameError(err)) as i32;
            }
        };
        let build_id = match get_build_id(info) {
            Some(build_id) => build_id,
            None => {
                return visitor.error(Error::BuildIDError) as i32;
            }
        };
        visitor.dso(Dso {
            name: name,
            build_id: build_id,
            phdrs: info.phdr_slice(),
            base: info.addr as usize,
        }) as i32
    }
    unsafe { dl_iterate_phdr(callback, &mut visitor) };
}

struct DsoPrinter<F: std::io::Write> {
    writer: F,
    module_count: usize,
    error: Result<(), std::io::Error>,
}

impl<F: std::io::Write> DsoVisitor for DsoPrinter<F> {
    fn dso(&mut self, dso: Dso<'_>) -> bool {
        let mut write = || {
            write!(
                self.writer,
                "{{{{{{module:{:#x}:{}:elf:{}}}}}}}\n",
                self.module_count,
                dso.name,
                HexSlice { bytes: dso.build_id.as_ref() }
            )?;
            for seg in dso.segments() {
                write!(
                    self.writer,
                    "{{{{{{mmap:{:#x}:{:#x}:load:{:#x}:{}:{:#x}}}}}}}\n",
                    seg.addr, seg.size, self.module_count, seg.flags, seg.mod_rel_addr
                )?;
            }
            self.module_count += 1;
            Ok(())
        };
        match write() {
            Ok(()) => false,
            Err(err) => {
                self.error = Err(err);
                true
            }
        }
    }
    fn error(&mut self, _error: Error) -> bool {
        false
    }
}

/// This function prints the Fuchsia symbolizer markup for all information
/// contained in a DSO.
///
/// # Arguments
///
/// * `out` - An implementation of std::io::Write to print markup to.
///
/// # Example
/// ```
/// print_dso_context(io::stderr());
/// ```
pub fn print_dso_context<F: std::io::Write>(out: &mut F) -> Result<(), std::io::Error> {
    write!(out, "{{{{{{reset}}}}}}\n")?;
    let mut visitor = DsoPrinter { writer: out, module_count: 0, error: Ok(()) };
    for_each_dso(&mut visitor);
    visitor.error
}

struct SnapshotVisitor {
    out: Vec<OwnedDso>,
}

impl DsoVisitor for SnapshotVisitor {
    fn dso(&mut self, dso: Dso<'_>) -> bool {
        self.out.push(OwnedDso::from(dso));
        false
    }
    fn error(&mut self, _error: Error) -> bool {
        false
    }
}

/// This function saves the current state of all DSOs linked into the process.
/// When combined with an unresolved backtrace snapshot this gives enough
/// information to symbolize the resulting markup.
pub fn snapshot() -> Vec<OwnedDso> {
    let mut out = SnapshotVisitor { out: vec![] };
    for_each_dso(&mut out);
    out.out
}

#[cfg(test)]
mod test {

    use super::*;

    #[test]
    fn test_snapshot() {
        let snap = snapshot();
        // We can expect that not only will these libraries have these names but
        // also that they will occur in exactly this order. If we get variants in
        // rust these assumptions will be violated however.
        let names = ["", "<vDSO>", "libfdio.so", "libc.so"];
        assert_eq!(snap.len(), names.len());
        for (name, dso) in names.iter().zip(snap) {
            assert_eq!(*name, dso.name);
            // There are many other things that need to be tested but that are hard
            // or impossible to test because the values change randomlly or as the
            // build changes.
            // We could check that all phdr's are in range from this side but nothing
            // more. Via an host-side test we could run this, parse it, and then,
            // verify the build ID, and then check the other program header details.
            // It's not clear to me that have the facilities to do that kind of
            // testing now however.
        }
    }

    #[test]
    fn test_print_dso_context() {
        // It would be nice to test that the output is valid but that's also hard.
        let mut buf = Vec::<u8>::new();
        print_dso_context(&mut buf).unwrap();
    }

}
