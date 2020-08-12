#!/usr/bin/env python3.8
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import print_function

from contextlib import contextmanager
from collections import namedtuple
import argparse
import json
import mmap
import os
import struct
import sys
import uuid

# Standard ELF constants.
ELFMAG = b'\x7fELF'
EI_CLASS = 4
ELFCLASS32 = 1
ELFCLASS64 = 2
EI_DATA = 5
ELFDATA2LSB = 1
ELFDATA2MSB = 2
EM_386 = 3
EM_ARM = 40
EM_X86_64 = 62
EM_AARCH64 = 183
PT_LOAD = 1
PT_DYNAMIC = 2
PT_INTERP = 3
PT_NOTE = 4
PT_GNU_RELRO = 0x6474e552
PF_X = 1
PF_W = 2
DT_NEEDED = 1
DT_PLTRELSZ = 2
DT_STRTAB = 5
DT_RELA = 7
DT_RELASZ = 8
DT_REL = 17
DT_RELSZ = 18
DT_RELRSZ = 35
DT_RELR = 36
DT_PLTREL = 20
DT_SONAME = 14
DT_RELACOUNT = 0x6ffffff9
DT_RELCOUNT = 0x6ffffffa
NT_GNU_BUILD_ID = 3
SHT_SYMTAB = 2
SHF_ALLOC = 2

IS_PYTHON3 = sys.version_info[0] >= 3

class elf_note(namedtuple('elf_note', [
        'name',
        'type',
        'desc',
])):

    # An ELF note is identified by (name_string, type_integer).
    def ident(self):
        return (self.name, self.type)

    def is_build_id(self):
        return self.ident() == ('GNU\0', NT_GNU_BUILD_ID)

    def build_id_hex(self):
        if self.is_build_id():
            return ''.join(('%02x' % byte) for byte in self.desc)
        return None

    def __repr__(self):
        return (
            'elf_note(%r, %#x, <%d bytes>)' %
            (self.name, self.type, len(self.desc)))


def gen_elf():
    # { 'Struct1': (ELFCLASS32 fields, ELFCLASS64 fields),
    #   'Struct2': fields_same_for_both, ... }
    elf_types = {
        'Ehdr':
            (
                [
                    ('e_ident', '16s'),
                    ('e_type', 'H'),
                    ('e_machine', 'H'),
                    ('e_version', 'I'),
                    ('e_entry', 'I'),
                    ('e_phoff', 'I'),
                    ('e_shoff', 'I'),
                    ('e_flags', 'I'),
                    ('e_ehsize', 'H'),
                    ('e_phentsize', 'H'),
                    ('e_phnum', 'H'),
                    ('e_shentsize', 'H'),
                    ('e_shnum', 'H'),
                    ('e_shstrndx', 'H'),
                ], [
                    ('e_ident', '16s'),
                    ('e_type', 'H'),
                    ('e_machine', 'H'),
                    ('e_version', 'I'),
                    ('e_entry', 'Q'),
                    ('e_phoff', 'Q'),
                    ('e_shoff', 'Q'),
                    ('e_flags', 'I'),
                    ('e_ehsize', 'H'),
                    ('e_phentsize', 'H'),
                    ('e_phnum', 'H'),
                    ('e_shentsize', 'H'),
                    ('e_shnum', 'H'),
                    ('e_shstrndx', 'H'),
                ]),
        'Phdr':
            (
                [
                    ('p_type', 'I'),
                    ('p_offset', 'I'),
                    ('p_vaddr', 'I'),
                    ('p_paddr', 'I'),
                    ('p_filesz', 'I'),
                    ('p_memsz', 'I'),
                    ('p_flags', 'I'),
                    ('p_align', 'I'),
                ], [
                    ('p_type', 'I'),
                    ('p_flags', 'I'),
                    ('p_offset', 'Q'),
                    ('p_vaddr', 'Q'),
                    ('p_paddr', 'Q'),
                    ('p_filesz', 'Q'),
                    ('p_memsz', 'Q'),
                    ('p_align', 'Q'),
                ]),
        'Shdr':
            (
                [
                    ('sh_name', 'L'),
                    ('sh_type', 'L'),
                    ('sh_flags', 'L'),
                    ('sh_addr', 'L'),
                    ('sh_offset', 'L'),
                    ('sh_size', 'L'),
                    ('sh_link', 'L'),
                    ('sh_info', 'L'),
                    ('sh_addralign', 'L'),
                    ('sh_entsize', 'L'),
                ], [
                    ('sh_name', 'L'),
                    ('sh_type', 'L'),
                    ('sh_flags', 'Q'),
                    ('sh_addr', 'Q'),
                    ('sh_offset', 'Q'),
                    ('sh_size', 'Q'),
                    ('sh_link', 'L'),
                    ('sh_info', 'L'),
                    ('sh_addralign', 'Q'),
                    ('sh_entsize', 'Q'),
                ]),
        'Dyn':
            (
                [
                    ('d_tag', 'i'),
                    ('d_val', 'I'),
                ], [
                    ('d_tag', 'q'),
                    ('d_val', 'Q'),
                ]),
        'Nhdr': [
            ('n_namesz', 'I'),
            ('n_descsz', 'I'),
            ('n_type', 'I'),
        ],
        'dwarf2_line_header':
            [
                ('unit_length', 'L'),
                ('version', 'H'),
                ('header_length', 'L'),
                ('minimum_instruction_length', 'B'),
                ('default_is_stmt', 'B'),
                ('line_base', 'b'),
                ('line_range', 'B'),
                ('opcode_base', 'B'),
            ],
        'dwarf4_line_header':
            [
                ('unit_length', 'L'),
                ('version', 'H'),
                ('header_length', 'L'),
                ('minimum_instruction_length', 'B'),
                ('maximum_operations_per_instruction', 'B'),
                ('default_is_stmt', 'B'),
                ('line_base', 'b'),
                ('line_range', 'b'),
                ('opcode_base', 'B'),
            ],
    }

    # There is an accessor for each struct, e.g. Ehdr.
    # Ehdr.read is a function like Struct.unpack_from.
    # Ehdr.size is the size of the struct.
    elf_accessor = namedtuple('elf_accessor', ['size', 'read', 'write', 'pack'])

    # All the accessors for a format (class, byte-order) form one elf,
    # e.g. use elf.Ehdr and elf.Phdr.
    elf = namedtuple('elf', list(elf_types.keys()))

    def gen_accessors(is64, struct_byte_order):

        def make_accessor(type, decoder):
            return elf_accessor(
                size=decoder.size,
                read=lambda buffer, offset=0: type._make(
                    decoder.unpack_from(buffer, offset)),
                write=lambda buffer, offset, x: decoder.pack_into(
                    buffer, offset, *x),
                pack=lambda x: decoder.pack(*x))

        for name, fields in elf_types.items():
            if isinstance(fields, tuple):
                fields = fields[1 if is64 else 0]
            type = namedtuple(name, [field_name for field_name, fmt in fields])
            decoder = struct.Struct(
                struct_byte_order + ''.join(fmt for field_name, fmt in fields))
            yield make_accessor(type, decoder)

    for elfclass, is64 in [(ELFCLASS32, False), (ELFCLASS64, True)]:
        for elf_bo, struct_bo in [(ELFDATA2LSB, '<'), (ELFDATA2MSB, '>')]:
            yield (
                (elfclass, elf_bo),
                elf(*gen_accessors(is64, struct_bo)))


# e.g. ELF[file[EI_CLASS], file[EI_DATA]].Ehdr.read(file).e_phnum
ELF = dict(gen_elf())


def get_elf_accessor(file):
    # If it looks like an ELF file, whip out the decoder ring.
    if file[:len(ELFMAG)] == ELFMAG:
        return ELF.get((ord(file[EI_CLASS:EI_CLASS+1]), ord(file[EI_DATA:EI_DATA+1])), None)
    return None


def gen_phdrs(file, elf, ehdr):
    for pos in range(0, ehdr.e_phnum * elf.Phdr.size, elf.Phdr.size):
        yield elf.Phdr.read(file, ehdr.e_phoff + pos)


def gen_shdrs(file, elf, ehdr):
    for pos in range(0, ehdr.e_shnum * elf.Shdr.size, elf.Shdr.size):
        yield elf.Shdr.read(file, ehdr.e_shoff + pos)


cpu = namedtuple(
    'cpu',
    [
        'e_machine',  # ELF e_machine int
        'llvm',  # LLVM triple CPU component
        'gn',  # GN target_cpu
    ])

ELF_MACHINE_TO_CPU = {
    elf: cpu(elf, llvm, gn) for elf, llvm, gn in [
        (EM_386, 'i386', 'x86'),
        (EM_ARM, 'arm', 'arm'),
        (EM_X86_64, 'x86_64', 'x64'),
        (EM_AARCH64, 'aarch64', 'arm64'),
    ]
}


@contextmanager
def mmapper(filename):
    """A context manager that yields (fd, file_contents) given a file name.
This ensures that the mmap and file objects are closed at the end of the
'with' statement."""
    fileobj = open(filename, 'rb')
    fd = fileobj.fileno()
    size = os.fstat(fd).st_size
    if size == 0:
        # mmap can't handle empty files.
        try:
            yield fd, '', 0
        finally:
            fileobj.close()
    else:
        mmapobj = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)
        try:
            yield fd, mmapobj, size
        finally:
            mmapobj.close()
            fileobj.close()


def makedirs(dirs):
    try:
        os.makedirs(dirs)
    except OSError as e:
        if e.errno != os.errno.EEXIST:
            raise e


elf_sizes = namedtuple(
    'elf_sizes', [
        'file',                     # st_size
        'memory',                   # lowest p_vaddr to highest p_memsz
        'rel',                      # DT_RELSZ (+ DT_PLTRELSZ if matching)
        'rela',                     # DT_RELASZ (+ DT_PLTRELSZ if matching)
        'relr',                     # DT_RELRSZ
        'relcount',                 # DT_RELCOUNT + DT_RELACOUNT
        'relro',                    # PT_GNU_RELRO p_memsz
        'code',                     # executable segment p_filesz
        'data',                     # writable segment p_filesz
        'bss',                      # writable segment p_memsz past p_filesz
    ])

# elf_info objects are only created by `get_elf_info` or the `copy` or
# `rename` methods.
class elf_info(namedtuple(
        'elf_info',
    [
        'filename',
        'sizes',                # elf_sizes
        'cpu',                  # cpu tuple
        'notes',                # list of (ident, desc): selected notes
        'build_id',             # string: lowercase hex
        'stripped',             # bool: Has no symbols or .debug_* sections
        'interp',               # string or None: PT_INTERP (without \0)
        'soname',               # string or None: DT_SONAME
        'needed',               # list of strings: DT_NEEDED
    ])):

    def rename(self, filename):
        assert os.path.samefile(self.filename, filename)
        return self._replace(filename=filename)

    def with_soname(self, soname):
        return self._replace(soname=soname)

    def copy(self):
        return self._replace()

    def _replace(self, **kwargs):
        # Copy the tuple.
        clone = self.__class__(*super()._replace(**kwargs))
        # Copy the lazy state.
        clone.elf = self.elf
        if self.get_sources == clone.get_sources:
            raise Exception("uninitialized elf_info object!")
        clone.get_sources = self.get_sources
        return clone

    # This is replaced with a closure by the creator in get_elf_info.
    def get_sources(self):
        raise Exception("uninitialized elf_info object!")

    def strip(self, stripped_filename):
        """Write stripped output to the given file unless it already exists
with identical contents.  Returns True iff the file was changed."""
        with mmapper(self.filename) as mapped:
            fd, file, filesize = mapped
            ehdr = self.elf.Ehdr.read(file)

            stripped_ehdr = ehdr._replace(e_shoff=0, e_shnum=0, e_shstrndx=0)
            stripped_size = max(
                phdr.p_offset + phdr.p_filesz
                for phdr in gen_phdrs(file, self.elf, ehdr)
                if phdr.p_type == PT_LOAD)
            assert ehdr.e_phoff + (
                ehdr.e_phnum * ehdr.e_phentsize) <= stripped_size

            def gen_stripped_contents():
                yield self.elf.Ehdr.pack(stripped_ehdr)
                yield file[self.elf.Ehdr.size:stripped_size]

            def old_file_matches():
                old_size = os.path.getsize(stripped_filename)
                new_size = sum(len(x) for x in gen_stripped_contents())
                if old_size != new_size:
                    return False
                with open(stripped_filename, 'rb') as f:
                    for chunk in gen_stripped_contents():
                        if f.read(len(chunk)) != chunk:
                            return False
                return True

            if os.path.exists(stripped_filename):
                if old_file_matches():
                    return False
                else:
                    os.remove(stripped_filename)
            # Create the new file with the same mode as the original.
            with os.fdopen(os.open(stripped_filename,
                                   os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                                   os.fstat(fd).st_mode & 0o777),
                           'wb') as stripped_file:
                stripped_file.write(self.elf.Ehdr.pack(stripped_ehdr))
                stripped_file.write(file[self.elf.Ehdr.size:stripped_size])
            return True


def get_elf_info(filename, match_notes=False):
    file = None
    elf = None
    ehdr = None
    phdrs = None

    # Yields an elf_note for each note in any PT_NOTE segment.
    def gen_notes():

        def round_up_to(size):
            return ((size + 3) // 4) * 4

        for phdr in phdrs:
            if phdr.p_type == PT_NOTE:
                pos = phdr.p_offset
                while pos < phdr.p_offset + phdr.p_filesz:
                    nhdr = elf.Nhdr.read(file, pos)
                    pos += elf.Nhdr.size
                    # TODO(fxbug.dev/55190) PT_NOTE is expected to be valid
                    # 7-bit ASCII or UTF-8. Remove this when clients no longer
                    # depend on that behavior.
                    name = str(file[pos:pos + nhdr.n_namesz].decode())
                    pos += round_up_to(nhdr.n_namesz)
                    # PT_DESC is not always string-ish, just copy the bytes.
                    if IS_PYTHON3:
                      desc = file[pos:pos + nhdr.n_descsz]
                    else:
                      desc = [ord(b) for b in file[pos:pos + nhdr.n_descsz]]
                    pos += round_up_to(nhdr.n_descsz)
                    yield elf_note(name, nhdr.n_type, desc)

    def gen_sections():
        shdrs = list(gen_shdrs(file, elf, ehdr))
        if not shdrs:
            return
        strtab_shdr = shdrs[ehdr.e_shstrndx]
        for shdr, i in zip(shdrs, range(len(shdrs))):
            if i == 0:
                continue
            assert shdr.sh_name < strtab_shdr.sh_size, (
                "%s: invalid sh_name" % filename)
            yield (shdr, extract_C_string(strtab_shdr.sh_offset + shdr.sh_name))

    # Generates '\0'-terminated strings starting at the given offset,
    # until an empty string.
    def gen_strings(start):
        while True:
            end = file.find(b'\0', start)
            assert end >= start, (
                "%s: Unterminated string at %#x" % (filename, start))
            if start == end:
                break
            yield file[start:end].decode()
            start = end + 1

    def extract_C_string(start):
        for string in gen_strings(start):
            return string
        return ''

    # Returns a string of hex digits (or None).
    def get_build_id():
        build_id = None
        for note in gen_notes():
            # Note that the last build_id note needs to be used due to TO-442.
            possible_build_id = note.build_id_hex()
            if possible_build_id:
                build_id = possible_build_id
        return build_id

    # Returns a list of elf_note objects.
    def get_matching_notes():
        if isinstance(match_notes, bool):
            if match_notes:
                return list(gen_notes())
            else:
                return []
        # If not a bool, it's an iterable of ident pairs.
        return [note for note in gen_notes() if note.ident() in match_notes]

    # Returns a string (without trailing '\0'), or None.
    def get_interp():
        # PT_INTERP points directly to a string in the file.
        for interp in (phdr for phdr in phdrs if phdr.p_type == PT_INTERP):
            interp = file[interp.p_offset:interp.p_offset + interp.p_filesz]
            if interp[-1:] == b'\0':
                interp = interp[:-1]
            return interp.decode()
        return None

    def get_dynamic():
        # PT_DYNAMIC points to the list of ElfNN_Dyn tags.
        for dynamic in (phdr for phdr in phdrs if phdr.p_type == PT_DYNAMIC):
            return [
                elf.Dyn.read(file, dynamic.p_offset + dyn_offset)
                for dyn_offset in range(0, dynamic.p_filesz, elf.Dyn.size)
            ]
        return None

    def dyn_get(dyn, tag):
        for dt in dyn:
            if dt.d_tag == tag:
                return dt.d_val
        return None

    # Returns a string (or None) and a set of strings.
    def get_soname_and_needed(dyn):
        # Each DT_NEEDED or DT_SONAME points to a string in the .dynstr table.
        def GenDTStrings(tag):
            return (
                extract_C_string(strtab_offset + dt.d_val)
                for dt in dyn
                if dt.d_tag == tag)

        if dyn is None:
          return None, set()

        # DT_STRTAB points to the string table's vaddr (.dynstr).
        strtab_vaddr = dyn_get(dyn, DT_STRTAB)

        # Find the PT_LOAD containing the vaddr to compute the file offset.
        [strtab_offset] = [
            strtab_vaddr - phdr.p_vaddr + phdr.p_offset
            for phdr in phdrs
                if (
                    phdr.p_type == PT_LOAD and phdr.p_vaddr <= strtab_vaddr and
                    strtab_vaddr - phdr.p_vaddr < phdr.p_filesz)
        ]

        soname = None
        for soname in GenDTStrings(DT_SONAME):
            break

        return soname, set(GenDTStrings(DT_NEEDED))

    def get_stripped():
        return all(
            (shdr.sh_flags & SHF_ALLOC) != 0 or
            (shdr.sh_type != SHT_SYMTAB and not name.startswith('.debug_'))
            for shdr, name in gen_sections())

    def get_memory_size():
        segments = [phdr for phdr in phdrs if phdr.p_type == PT_LOAD]
        first = segments[0]
        last = segments[-1]
        start = first.p_vaddr &- first.p_align
        last = (last.p_vaddr + last.p_memsz + last.p_align - 1) &- last.p_align
        return last - start

    def get_relro_size():
        return sum(phdr.p_memsz for phdr in phdrs
                   if phdr.p_type == PT_GNU_RELRO)

    def get_segment_size(phdr, file, mem):
        start = phdr.p_vaddr &- phdr.p_align
        file_end = phdr.p_vaddr + phdr.p_filesz
        mem_end = phdr.p_vaddr + phdr.p_memsz
        file_end = (file_end + phdr.p_align - 1) &- phdr.p_align
        mem_end = (mem_end + phdr.p_align - 1) &- phdr.p_align
        if file and mem:
            return mem_end - start
        if file:
            return file_end - start
        return mem_end - file_end

    def gen_executable_segments():
        return (phdr for phdr in phdrs
                if phdr.p_type == PT_LOAD and (phdr.p_flags & PF_X) != 0)

    def gen_writable_segments():
        return (phdr for phdr in phdrs
                if phdr.p_type == PT_LOAD and (phdr.p_flags & PF_W) != 0)

    def get_code_size():
        return sum(get_segment_size(phdr, file=True, mem=False)
                   for phdr in gen_executable_segments())

    def get_data_size():
        return sum(get_segment_size(phdr, file=True, mem=False)
                   for phdr in gen_writable_segments())

    def get_bss_size():
        return sum(get_segment_size(phdr, file=False, mem=True)
                   for phdr in gen_writable_segments())

    def get_relsz(dyn, tag, sizetag):
        if dyn is None:
            return 0
        relsz = dyn_get(dyn, sizetag)
        if relsz is None:
            relsz = 0
        if dyn_get(dyn, DT_PLTREL) == tag:
            pltrelsz = dyn_get(dyn, DT_PLTRELSZ)
            if pltrelsz is not None:
                relsz += pltrelsz
        return relsz

    def get_relcount(dyn):
        if dyn is None:
            return 0
        return (dyn_get(dyn, DT_RELCOUNT) or 0) + (dyn_get(dyn, DT_RELACOUNT) or 0)

    def get_cpu():
        return ELF_MACHINE_TO_CPU.get(ehdr.e_machine)

    def gen_source_files():
        # Given the file position of a CU header (starting with the
        # beginning of the .debug_line section), return the position
        # of the include_directories portion and the position of the
        # next CU header.
        def read_line_header(pos):
            # Decode DWARF .debug_line per-CU header.
            hdr_type = elf.dwarf2_line_header
            hdr = hdr_type.read(file, pos)
            assert hdr.unit_length < 0xfffffff0, ("%s: 64-bit DWARF" % filename)
            assert hdr.version in [
                2, 3, 4
            ], ("%s: DWARF .debug_line version %r" % (filename, hdr.version))
            if hdr.version == 4:
                hdr_type = elf.dwarf4_line_header
                hdr = hdr_type.read(file, pos)
            return (
                pos + hdr_type.size + hdr.opcode_base - 1,
                pos + 4 + hdr.unit_length)

        # Decode include_directories portion of DWARF .debug_line format.
        def read_include_dirs(pos):
            include_dirs = list(gen_strings(pos))
            pos += sum(len(dir) + 1 for dir in include_dirs) + 1
            return pos, include_dirs

        # Decode file_paths portion of DWARF .debug_line format.
        def gen_file_paths(start, limit):
            while start < limit:
                end = file.find(b'\0', start, limit)
                assert end >= start, (
                    "%s: Unterminated string at %#x" % (filename, start))
                if start == end:
                    break
                name = file[start:end].decode()
                start = end + 1
                # Decode 3 ULEB128s to advance start, but only use the first.
                for i in range(3):
                    value = 0
                    bits = 0
                    while start < limit:
                        # TODO(48946): Remove this single-element slice hack
                        # once Python2 is no longer used in-tree.
                        byte = ord(file[start:start+1])
                        start += 1
                        value |= (byte & 0x7f) << bits
                        if (byte & 0x80) == 0:
                            break
                        bits += 7
                    if i == 0:
                        include_idx = value
                # Ignore the fake file names the compiler leaks into the DWARF.
                if name not in ['<stdin>', '<command-line>']:
                    yield name, include_idx

        for shdr, name in gen_sections():
            if name == '.debug_line':
                next = shdr.sh_offset
                while next < shdr.sh_offset + shdr.sh_size:
                    pos, next = read_line_header(next)

                    pos, include_dirs = read_include_dirs(pos)
                    assert pos <= next

                    # 0 means relative to DW_AT_comp_dir, which should be ".".
                    # Indices into the actual table start at 1.
                    include_dirs.insert(0, '')

                    # Decode file_paths and apply include directories.
                    for name, i in gen_file_paths(pos, next):
                        name = os.path.join(include_dirs[i], name)
                        yield os.path.normpath(name)

    # This closure becomes the elf_info object's `get_sources` method.
    def lazy_get_sources():
        # Run the generator and cache its results as a set.
        sources_cache = set(gen_source_files())
        # Replace the method to just return the cached set next time.
        info.get_sources = lambda: sources_cache
        return sources_cache

    # Map in the whole file's contents and use it as a string.
    with mmapper(filename) as mapped:
        fd, file, filesize = mapped
        elf = get_elf_accessor(file)
        if elf is not None:
            # ELF header leads to program headers.
            ehdr = elf.Ehdr.read(file)
            assert ehdr.e_phentsize == elf.Phdr.size, (
                "%s: invalid e_phentsize" % filename)
            phdrs = list(gen_phdrs(file, elf, ehdr))
            dyn = get_dynamic()
            info = elf_info(
                filename,
                elf_sizes(file=filesize,
                          memory=get_memory_size(),
                          code=get_code_size(),
                          relro=get_relro_size(),
                          data=get_data_size(),
                          bss=get_bss_size(),
                          rel=get_relsz(dyn, DT_REL, DT_RELSZ),
                          rela=get_relsz(dyn, DT_RELA, DT_RELASZ),
                          relr=get_relsz(dyn, DT_RELR, DT_RELRSZ),
                          relcount=get_relcount(dyn)),
                get_cpu(), get_matching_notes(), get_build_id(),
                get_stripped(), get_interp(), *get_soname_and_needed(dyn))
            info.elf = elf
            info.get_sources = lazy_get_sources
            return info

    return None


# Module public API.
__all__ = ['cpu', 'elf_info', 'elf_note', 'get_elf_accessor', 'get_elf_info']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--strip', action='store_true',
                        help='Strip each file into FILE.ei-strip')
    parser.add_argument('--build-id', action='store_true',
                        help='Print each build ID')
    parser.add_argument('--bootfs-dir', help='bootfs content', metavar='DIR')
    parser.add_argument('--zbi', help='Read zbi.json', metavar='FILE')
    parser.add_argument('--blobs', help='Read blobs.json', metavar='FILE')
    parser.add_argument('--sizes', help='Write elf_sizes.json', metavar='FILE')
    parser.add_argument('file', help='ELF file', metavar='FILE', nargs='*')
    args = parser.parse_args()

    files = args.file
    if args.zbi:
        if not args.bootfs_dir:
            parser.exit('--zbi and --bootfs-dir must be specified together')
        bootfs_map = {}
        with open(args.zbi) as f:
            zbi = json.load(f)
        for entry in zbi:
            if entry['type'] != 'BOOTFS':
                continue
            for item in entry['contents']:
                filepath = os.path.join(args.bootfs_dir, item['name'])
                files.append(filepath)
                bootfs_map[filepath] = item
    if args.blobs:
        with open(args.blobs) as f:
            blobs = json.load(f)
        files += [blob['source_path'] for blob in blobs]

    if args.sizes:
        if args.blobs:
            blob_map = {blob['source_path']: blob for blob in blobs}
        def json_size(info):
            sizes = info.sizes._asdict()
            sizes['path'] = info.filename
            sizes['build_id'] = info.build_id
            if args.zbi and info.filename in bootfs_map:
                item = bootfs_map[info.filename]
                assert info.sizes.file == item['length'], (
                    "%r != %r in %r vs %r" % (info.sizes.file, blob['length'],
                                              info, blob))
                sizes['zbi'] = item['size']
            if args.blobs and info.filename in blob_map:
                blob = blob_map[info.filename]
                assert info.sizes.file == blob['bytes'], (
                    "%r != %r in %r vs %r" % (info.sizes.file, blob['bytes'],
                                              info, blob))
                sizes['blob'] = blob['size']
            return sizes
        # Sort for stable output.
        sizes = sorted([json_size(info)
                        for info in (get_elf_info(file) for file in files)
                        if info],
                       key=lambda info: info['path'])
        totals = {}
        for file in sizes:
            for key, val in file.items():
                if isinstance(val, int):
                    totals[key] = totals.get(key, 0) + val
        with open(args.sizes, 'w') as outf:
            json.dump({'files': sizes, 'totals': totals},
                      outf, sort_keys=True, indent=1)
    else:
        for file in files:
          info = get_elf_info(file)

          if args.strip:
              stripped_filename = info.filename + '.ei-strip'
              info.strip(stripped_filename)
              print(stripped_filename)
          elif args.build_id:
              print(info.build_id)
          else:
              print(info)
              for source in info.get_sources():
                  print('\t' + source)

    return 0


if __name__ == '__main__':
    sys.exit(main())
