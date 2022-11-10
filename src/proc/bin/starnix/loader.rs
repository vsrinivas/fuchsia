// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, sys::zx_thread_state_general_regs_t, AsHandleRef, HandleBased};
use process_builder::{elf_load, elf_parse};
use std::ffi::{CStr, CString};
use std::sync::Arc;
use zerocopy::AsBytes;

use crate::fs::FileHandle;
use crate::logging::*;
use crate::mm::*;
use crate::task::*;
use crate::types::*;

fn populate_initial_stack(
    stack_vmo: &zx::Vmo,
    path: &CStr,
    argv: &Vec<CString>,
    environ: &Vec<CString>,
    mut auxv: Vec<(u32, u64)>,
    stack_base: UserAddress,
    original_stack_start_addr: UserAddress,
) -> Result<UserAddress, Errno> {
    let mut stack_pointer = original_stack_start_addr;
    let write_stack = |data: &[u8], addr: UserAddress| {
        stack_vmo
            .write(data, (addr - stack_base) as u64)
            .map_err(|status| from_status_like_fdio!(status))
    };

    let mut string_data = vec![];
    for arg in argv {
        string_data.extend_from_slice(arg.as_bytes_with_nul());
    }
    for env in environ {
        string_data.extend_from_slice(env.as_bytes_with_nul());
    }
    stack_pointer -= string_data.len();
    let strings_addr = stack_pointer;
    write_stack(string_data.as_slice(), strings_addr)?;

    // Write the path used with execve.
    stack_pointer -= path.to_bytes_with_nul().len();
    let execfn_addr = stack_pointer;
    write_stack(path.to_bytes_with_nul(), execfn_addr)?;

    let mut random_seed = [0; 16];
    zx::cprng_draw(&mut random_seed);
    stack_pointer -= random_seed.len();
    let random_seed_addr = stack_pointer;
    write_stack(&random_seed, random_seed_addr)?;

    auxv.push((AT_EXECFN, execfn_addr.ptr() as u64));
    auxv.push((AT_RANDOM, random_seed_addr.ptr() as u64));
    auxv.push((AT_NULL, 0));

    // After the remainder (argc/argv/environ/auxv) is pushed, the stack pointer must be 16 byte
    // aligned. This is required by the ABI and assumed by the compiler to correctly align SSE
    // operations. But this can't be done after it's pushed, since it has to be right at the top of
    // the stack. So we collect it all, align the stack appropriately now that we know the size,
    // and push it all at once.
    let mut main_data = vec![];
    // argc
    let argc: u64 = argv.len() as u64;
    main_data.extend_from_slice(&argc.to_ne_bytes());
    // argv
    const ZERO: [u8; 8] = [0; 8];
    let mut next_string_addr = strings_addr;
    for arg in argv {
        main_data.extend_from_slice(&next_string_addr.ptr().to_ne_bytes());
        next_string_addr += arg.as_bytes_with_nul().len();
    }
    main_data.extend_from_slice(&ZERO);
    // environ
    for env in environ {
        main_data.extend_from_slice(&next_string_addr.ptr().to_ne_bytes());
        next_string_addr += env.as_bytes_with_nul().len();
    }
    main_data.extend_from_slice(&ZERO);
    // auxv
    for (tag, val) in auxv {
        main_data.extend_from_slice(&(tag as u64).to_ne_bytes());
        main_data.extend_from_slice(&(val as u64).to_ne_bytes());
    }

    // Time to push.
    stack_pointer -= main_data.len();
    stack_pointer -= stack_pointer.ptr() % 16;
    write_stack(main_data.as_slice(), stack_pointer)?;

    Ok(stack_pointer)
}

struct LoadedElf {
    headers: elf_parse::Elf64Headers,
    file_base: usize,
    vaddr_bias: usize,
    vmo: zx::Vmo,
}

// TODO: Improve the error reporting produced by this function by mapping ElfParseError to Errno more precisely.
fn elf_parse_error_to_errno(err: elf_parse::ElfParseError) -> Errno {
    log_warn!("elf parse error: {:?}", err);
    errno!(EINVAL)
}

// TODO: Improve the error reporting produced by this function by mapping ElfLoadError to Errno more precisely.
fn elf_load_error_to_errno(err: elf_load::ElfLoadError) -> Errno {
    log_warn!("elf load error: {:?}", err);
    errno!(EINVAL)
}

struct Mapper<'a> {
    file: &'a FileHandle,
    mm: &'a MemoryManager,
}
impl elf_load::Mapper for Mapper<'_> {
    fn map(
        &self,
        vmar_offset: usize,
        vmo: &zx::Vmo,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<usize, zx::Status> {
        let vmo = Arc::new(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?);
        self.mm
            .map(
                DesiredAddress::Fixed(self.mm.base_addr + vmar_offset),
                vmo,
                vmo_offset,
                length,
                flags,
                MappingOptions::empty(),
                Some(self.file.name.clone()),
            )
            .map_err(|e| {
                // TODO: Find a way to propagate this errno to the caller.
                log_error!("elf map error: {:?}", e);
                zx::Status::INVALID_ARGS
            })
            .map(|addr| addr.ptr())
    }
}

fn load_elf(elf: FileHandle, elf_vmo: zx::Vmo, mm: &MemoryManager) -> Result<LoadedElf, Errno> {
    let headers = elf_parse::Elf64Headers::from_vmo(&elf_vmo).map_err(elf_parse_error_to_errno)?;
    let elf_info = elf_load::loaded_elf_info(&headers);
    let file_base = match headers.file_header().elf_type() {
        Ok(elf_parse::ElfType::SharedObject) => {
            mm.get_random_base(elf_info.high - elf_info.low).ptr()
        }
        Ok(elf_parse::ElfType::Executable) => elf_info.low,
        _ => return error!(EINVAL),
    };
    let vaddr_bias = file_base.wrapping_sub(elf_info.low);
    let mapper = Mapper { file: &elf, mm };
    elf_load::map_elf_segments(&elf_vmo, &headers, &mapper, mm.base_addr.ptr(), vaddr_bias)
        .map_err(elf_load_error_to_errno)?;
    Ok(LoadedElf { headers, file_base, vaddr_bias, vmo: elf_vmo })
}

pub struct ThreadStartInfo {
    pub entry: UserAddress,
    pub stack: UserAddress,

    /// The address of the DT_DEBUG entry.
    pub dt_debug_address: Option<UserAddress>,
}

impl ThreadStartInfo {
    pub fn to_registers(&self) -> zx_thread_state_general_regs_t {
        zx_thread_state_general_regs_t {
            rip: self.entry.ptr() as u64,
            rsp: self.stack.ptr() as u64,
            ..Default::default()
        }
    }
}

/// Holds a resolved ELF VMO and associated parameters necessary for an execve call.
pub struct ResolvedElf {
    /// A file handle to the resolved ELF executable.
    pub file: FileHandle,
    /// A VMO to the resolved ELF executable.
    pub vmo: zx::Vmo,
    /// An ELF interpreter, if specified in the ELF executable header.
    pub interp: Option<ResolvedInterpElf>,
    /// Arguments to be passed to the new process.
    pub argv: Vec<CString>,
    /// The environment to initialize for the new process.
    pub environ: Vec<CString>,
}

/// Holds a resolved ELF interpreter VMO.
pub struct ResolvedInterpElf {
    /// A file handle to the resolved ELF interpreter.
    file: FileHandle,
    /// A VMO to the resolved ELF interpreter.
    vmo: zx::Vmo,
}

// The magic bytes of a script file.
const HASH_BANG: &[u8; 2] = b"#!";
const MAX_RECURSION_DEPTH: usize = 4;

/// Resolves a file into a validated executable ELF, following script interpreters to a fixed
/// recursion depth. `argv` may change due to script interpreter logic.
pub fn resolve_executable(
    current_task: &CurrentTask,
    file: FileHandle,
    path: CString,
    argv: Vec<CString>,
    environ: Vec<CString>,
) -> Result<ResolvedElf, Errno> {
    resolve_executable_impl(current_task, file, path, argv, environ, 0)
}

/// Resolves a file into a validated executable ELF, following script interpreters to a fixed
/// recursion depth.
fn resolve_executable_impl(
    current_task: &CurrentTask,
    file: FileHandle,
    path: CString,
    argv: Vec<CString>,
    environ: Vec<CString>,
    recursion_depth: usize,
) -> Result<ResolvedElf, Errno> {
    if recursion_depth >= MAX_RECURSION_DEPTH {
        return error!(ELOOP);
    }
    let vmo =
        file.get_vmo(current_task, None, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_EXECUTE)?;
    let mut header = [0u8; 2];
    match vmo.read(&mut header, 0) {
        Ok(()) => {}
        Err(zx::Status::OUT_OF_RANGE) => {
            // The file is empty, or it would have at least one page allocated to it.
            return error!(ENOEXEC);
        }
        Err(_) => return error!(EINVAL),
    }
    if &header == HASH_BANG {
        resolve_script(current_task, vmo, path, argv, environ, recursion_depth)
    } else {
        resolve_elf(current_task, file, vmo, argv, environ)
    }
}

/// Resolves a #! script file into a validated executable ELF.
fn resolve_script(
    current_task: &CurrentTask,
    vmo: zx::Vmo,
    path: CString,
    argv: Vec<CString>,
    environ: Vec<CString>,
    recursion_depth: usize,
) -> Result<ResolvedElf, Errno> {
    // All VMOs have sizes in multiple of the system page size, so as long as we only read a page or
    // less, we should never read past the end of the VMO.
    // Since Linux 5.1, the max length of the interpreter following the #! is 255.
    const HEADER_BUFFER_CAP: usize = 255 + HASH_BANG.len();
    let mut buffer = [0u8; HEADER_BUFFER_CAP];
    vmo.read(&mut buffer, 0).map_err(|_| errno!(EINVAL))?;

    let mut args = parse_interpreter_line(&buffer)?;
    let interpreter = current_task.open_file(args[0].as_bytes(), OpenFlags::RDONLY)?;

    // Append the original script executable path as an argument.
    args.push(path);

    // Append the original arguments (minus argv[0]).
    args.extend(argv.into_iter().skip(1));

    // Recurse and resolve the interpreter executable
    resolve_executable_impl(
        current_task,
        interpreter,
        args[0].clone(),
        args,
        environ,
        recursion_depth + 1,
    )
}

/// Parses a "#!" byte string and extracts CString arguments. The byte string must contain an
/// ASCII newline character or null-byte, or else it is considered truncated and parsing will fail.
/// If the byte string is empty or contains only whitespace, parsing fails.
/// If successful, the returned `Vec` will have at least one element (the interpreter path).
fn parse_interpreter_line(line: &[u8]) -> Result<Vec<CString>, Errno> {
    // Assuming the byte string starts with "#!", truncate the input to end at the first newline or
    // null-byte. If not found, assume the input was truncated and fail parsing.
    let end = line.iter().position(|&b| b == b'\n' || b == 0).ok_or_else(|| errno!(EINVAL))?;
    let line = &line[HASH_BANG.len()..end];

    // Split the byte string at the first whitespace character (or end of line). The first part
    // is the interpreter path.
    let is_tab_or_space = |&b| b == b' ' || b == b'\t';
    let first_whitespace = line.iter().position(is_tab_or_space).unwrap_or(line.len());
    let (interpreter, rest) = line.split_at(first_whitespace);
    if interpreter.is_empty() {
        return error!(ENOEXEC);
    }

    // The second part is the optional argument. Trim the leading and trailing whitespace, but
    // treat the middle as a single argument, even if whitespace is encountered.
    let begin = rest.iter().position(|b| !is_tab_or_space(b)).unwrap_or(rest.len());
    let end = rest.iter().rposition(|b| !is_tab_or_space(b)).map(|b| b + 1).unwrap_or(rest.len());
    let optional_arg = &rest[begin..end];

    // SAFETY: `CString::new` can only fail if it encounters a null-byte, which we've made sure
    // the input won't have.
    Ok(if optional_arg.is_empty() {
        vec![CString::new(interpreter).unwrap()]
    } else {
        vec![CString::new(interpreter).unwrap(), CString::new(optional_arg).unwrap()]
    })
}

/// Resolves a file handle into a validated executable ELF.
fn resolve_elf(
    current_task: &CurrentTask,
    file: FileHandle,
    vmo: zx::Vmo,
    argv: Vec<CString>,
    environ: Vec<CString>,
) -> Result<ResolvedElf, Errno> {
    let elf_headers = elf_parse::Elf64Headers::from_vmo(&vmo).map_err(elf_parse_error_to_errno)?;
    let interp = if let Some(interp_hdr) = elf_headers
        .program_header_with_type(elf_parse::SegmentType::Interp)
        .map_err(|_| errno!(EINVAL))?
    {
        // The ELF header specified an ELF interpreter.
        // Read the path and load this ELF as well.
        let mut interp = vec![0; interp_hdr.filesz as usize];
        vmo.read(&mut interp, interp_hdr.offset as u64)
            .map_err(|status| from_status_like_fdio!(status))?;
        let interp = CStr::from_bytes_with_nul(&interp).map_err(|_| errno!(EINVAL))?;
        let interp_file = current_task.open_file(interp.to_bytes(), OpenFlags::RDONLY)?;
        let interp_vmo = interp_file.get_vmo(
            current_task,
            None,
            zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_EXECUTE,
        )?;
        Some(ResolvedInterpElf { file: interp_file, vmo: interp_vmo })
    } else {
        None
    };
    Ok(ResolvedElf { file, vmo, interp, argv, environ })
}

/// Loads a resolved ELF into memory, along with an interpreter if one is defined, and initializes
/// the stack.
pub fn load_executable(
    current_task: &CurrentTask,
    resolved_elf: ResolvedElf,
    original_path: &CStr,
) -> Result<ThreadStartInfo, Errno> {
    let main_elf = load_elf(resolved_elf.file, resolved_elf.vmo, &current_task.mm)?;
    let interp_elf = resolved_elf
        .interp
        .map(|interp| load_elf(interp.file, interp.vmo, &current_task.mm))
        .transpose()?;

    let entry_elf = interp_elf.as_ref().unwrap_or(&main_elf);
    let entry = UserAddress::from_ptr(
        entry_elf.headers.file_header().entry.wrapping_add(entry_elf.vaddr_bias),
    );

    let dt_debug_address = parse_debug_addr(&main_elf);

    // TODO(tbodt): implement MAP_GROWSDOWN and then reset this to 1 page. The current value of
    // this is based on adding 0x1000 each time a segfault appears.
    let stack_size: usize = 0xf0000;
    let stack_vmo = Arc::new(zx::Vmo::create(stack_size as u64).map_err(|_| errno!(ENOMEM))?);
    stack_vmo
        .as_ref()
        .set_name(CStr::from_bytes_with_nul(b"[stack]\0").unwrap())
        .map_err(impossible_error)?;
    let stack_base = current_task.mm.map(
        DesiredAddress::Hint(UserAddress::default()),
        Arc::clone(&stack_vmo),
        0,
        stack_size,
        zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
        MappingOptions::empty(),
        None,
    )?;
    let stack = stack_base + (stack_size - 8);

    // Fuchsia does not have a quantity that corresposnds to AT_CLKTCK,
    // but we provide a typical value expected in *nix here.
    const STARNIX_CLOCK_TICKS_PER_SEC: u64 = 100;

    let auxv = {
        let creds = current_task.creds();

        vec![
            (AT_UID, creds.uid as u64),
            (AT_EUID, creds.euid as u64),
            (AT_GID, creds.gid as u64),
            (AT_EGID, creds.egid as u64),
            (AT_BASE, interp_elf.map_or(0, |interp| interp.file_base as u64)),
            (AT_PAGESZ, *PAGE_SIZE),
            (AT_PHDR, main_elf.file_base.wrapping_add(main_elf.headers.file_header().phoff) as u64),
            (AT_PHNUM, main_elf.headers.file_header().phnum as u64),
            (
                AT_ENTRY,
                main_elf.vaddr_bias.wrapping_add(main_elf.headers.file_header().entry) as u64,
            ),
            (AT_CLKTCK, STARNIX_CLOCK_TICKS_PER_SEC),
            (AT_SECURE, 0),
        ]
    };
    let stack = populate_initial_stack(
        &stack_vmo,
        original_path,
        &resolved_elf.argv,
        &resolved_elf.environ,
        auxv,
        stack_base,
        stack,
    )?;

    let mut mm_state = current_task.mm.state.write();
    mm_state.stack_base = stack_base;
    mm_state.stack_size = stack_size;
    mm_state.stack_start = stack;

    Ok(ThreadStartInfo { entry, stack, dt_debug_address })
}

/// Parses the debug address (`DT_DEBUG`) from the provided ELF.
///
/// The debug address is read from the `elf_parse::SegmentType::Dynamic` program header,
/// if such a tag exists.
///
/// Returns `None` if no debug tag exists.
fn parse_debug_addr(elf: &LoadedElf) -> Option<UserAddress> {
    match elf.headers.program_header_with_type(elf_parse::SegmentType::Dynamic) {
        Ok(Some(dynamic_header)) => {
            const ENTRY_SIZE: usize = std::mem::size_of::<elf_parse::Elf64Dyn>();
            let mut header_bytes = vec![0u8; dynamic_header.filesz as usize];
            elf.vmo.read(&mut header_bytes, dynamic_header.offset as u64).ok()?;

            for offset in (0..(dynamic_header.filesz as usize)).step_by(ENTRY_SIZE) {
                let mut dyn_entry = elf_parse::Elf64Dyn::default();
                let entry_range = offset..(offset + ENTRY_SIZE);
                dyn_entry.as_bytes_mut().clone_from_slice(&header_bytes[entry_range]);

                if dyn_entry.tag() == Ok(elf_parse::Elf64DynTag::Debug) {
                    return Some(UserAddress::from(
                        elf.vaddr_bias.wrapping_add(dynamic_header.vaddr + offset) as u64,
                    ));
                }
            }

            None
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::*;
    use assert_matches::assert_matches;

    #[::fuchsia::test]
    fn test_trivial_initial_stack() {
        let stack_vmo = zx::Vmo::create(0x4000).expect("VMO creation should succeed.");
        let stack_base = UserAddress::from_ptr(0x3000_0000);
        let original_stack_start_addr = UserAddress::from_ptr(0x3000_1000);

        let path = CString::new(&b""[..]).unwrap();
        let argv = &vec![];
        let environ = &vec![];

        let stack_start_addr = populate_initial_stack(
            &stack_vmo,
            &path,
            argv,
            environ,
            vec![],
            stack_base,
            original_stack_start_addr,
        )
        .expect("Populate initial stack should succeed.");

        let argc_size: usize = 8;
        let argv_terminator_size: usize = 8;
        let environ_terminator_size: usize = 8;
        let aux_execfn_terminator_size: usize = 8;
        let aux_execfn: usize = 16;
        let aux_random: usize = 16;
        let aux_null: usize = 16;
        let random_seed: usize = 16;

        let mut payload_size = argc_size
            + argv_terminator_size
            + environ_terminator_size
            + aux_execfn_terminator_size
            + aux_execfn
            + aux_random
            + aux_null
            + random_seed;
        payload_size += payload_size % 16;

        assert_eq!(stack_start_addr, original_stack_start_addr - payload_size);
    }

    fn exec_hello_starnix(current_task: &mut CurrentTask) -> Result<(), Errno> {
        let argv = vec![CString::new("bin/hello_starnix").unwrap()];
        current_task.exec(argv[0].clone(), argv, vec![])?;
        Ok(())
    }

    #[::fuchsia::test]
    fn test_load_hello_starnix() {
        let (_kernel, mut current_task) = create_kernel_and_task_with_pkgfs();
        exec_hello_starnix(&mut current_task).expect("failed to load executable");
        assert!(current_task.mm.get_mapping_count() > 0);
    }

    #[::fuchsia::test]
    fn test_snapshot_hello_starnix() {
        let (kernel, mut current_task) = create_kernel_and_task_with_pkgfs();
        exec_hello_starnix(&mut current_task).expect("failed to load executable");

        let current2 = create_task(&kernel, "another-task");
        current_task.mm.snapshot_to(&current2.mm).expect("failed to snapshot mm");

        assert_eq!(current_task.mm.get_mapping_count(), current2.mm.get_mapping_count());
    }

    #[::fuchsia::test]
    fn test_parse_interpreter_line() {
        assert_matches!(parse_interpreter_line(b"#!"), Err(_));
        assert_matches!(parse_interpreter_line(b"#!\n"), Err(_));
        assert_matches!(parse_interpreter_line(b"#! \n"), Err(_));
        assert_matches!(parse_interpreter_line(b"#!/bin/bash"), Err(_));
        assert_eq!(
            parse_interpreter_line(b"#!/bin/bash\x00\n"),
            Ok(vec![CString::new("/bin/bash").unwrap()])
        );
        assert_eq!(
            parse_interpreter_line(b"#!/bin/bash\n"),
            Ok(vec![CString::new("/bin/bash").unwrap()])
        );
        assert_eq!(
            parse_interpreter_line(b"#!/bin/bash -e\n"),
            Ok(vec![CString::new("/bin/bash").unwrap(), CString::new("-e").unwrap()])
        );
        assert_eq!(
            parse_interpreter_line(b"#!/bin/bash -e \n"),
            Ok(vec![CString::new("/bin/bash").unwrap(), CString::new("-e").unwrap()])
        );
        assert_eq!(
            parse_interpreter_line(b"#!/bin/bash \t -e\n"),
            Ok(vec![CString::new("/bin/bash").unwrap(), CString::new("-e").unwrap()])
        );
        assert_eq!(
            parse_interpreter_line(b"#!/bin/bash -e -x\n"),
            Ok(vec![CString::new("/bin/bash").unwrap(), CString::new("-e -x").unwrap(),])
        );
        assert_eq!(
            parse_interpreter_line(b"#!/bin/bash -e  -x\t-l\n"),
            Ok(vec![CString::new("/bin/bash").unwrap(), CString::new("-e  -x\t-l").unwrap(),])
        );
        assert_eq!(
            parse_interpreter_line(b"#!/bin/bash\nfoobar"),
            Ok(vec![CString::new("/bin/bash").unwrap()])
        );
    }
}
