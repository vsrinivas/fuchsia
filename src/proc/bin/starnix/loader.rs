// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_io as fio;
use fuchsia_zircon::{self as zx, AsHandleRef, Status};
use process_builder::{elf_load, elf_parse};
use std::ffi::{CStr, CString};
use std::sync::Arc;

use crate::auth::*;
use crate::fs::{FileSystem, SyslogFile};
use crate::mm::PAGE_SIZE;
use crate::task::*;
use crate::uapi::*;

pub struct ProcessParameters {
    pub name: CString,
    pub argv: Vec<CString>,
    pub environ: Vec<CString>,
}

fn populate_initial_stack(
    stack_vmo: &zx::Vmo,
    params: &ProcessParameters,
    mut auxv: Vec<(u32, u64)>,
    stack_base: usize,
    original_stack_start_addr: usize,
) -> Result<usize, Status> {
    let mut stack_pointer = original_stack_start_addr;
    let write_stack = |data: &[u8], addr: usize| stack_vmo.write(data, (addr - stack_base) as u64);

    let mut string_data = vec![];
    for arg in &params.argv {
        string_data.extend_from_slice(arg.as_bytes_with_nul());
    }
    for env in &params.environ {
        string_data.extend_from_slice(env.as_bytes_with_nul());
    }
    stack_pointer -= string_data.len();
    let strings_addr = stack_pointer;
    write_stack(string_data.as_slice(), strings_addr)?;

    let mut random_seed = [0; 16];
    zx::cprng_draw(&mut random_seed)?;
    stack_pointer -= random_seed.len();
    let random_seed_addr = stack_pointer;
    write_stack(&random_seed, random_seed_addr)?;

    auxv.push((AT_RANDOM, random_seed_addr as u64));
    auxv.push((AT_NULL, 0));

    // After the remainder (argc/argv/environ/auxv) is pushed, the stack pointer must be 16 byte
    // aligned. This is required by the ABI and assumed by the compiler to correctly align SSE
    // operations. But this can't be done after it's pushed, since it has to be right at the top of
    // the stack. So we collect it all, align the stack appropriately now that we know the size,
    // and push it all at once.
    let mut main_data = vec![];
    // argc
    let argc: u64 = params.argv.len() as u64;
    main_data.extend_from_slice(&argc.to_ne_bytes());
    // argv
    const ZERO: [u8; 8] = [0; 8];
    let mut next_string_addr = strings_addr;
    for arg in &params.argv {
        main_data.extend_from_slice(&next_string_addr.to_ne_bytes());
        next_string_addr += arg.as_bytes_with_nul().len();
    }
    main_data.extend_from_slice(&ZERO);
    // environ
    for env in &params.environ {
        main_data.extend_from_slice(&next_string_addr.to_ne_bytes());
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
    stack_pointer -= stack_pointer % 16;
    write_stack(main_data.as_slice(), stack_pointer)?;

    return Ok(stack_pointer);
}

struct LoadedElf {
    headers: elf_parse::Elf64Headers,
    base: usize,
    bias: usize,
}

fn load_elf(vmo: &zx::Vmo, vmar: &zx::Vmar) -> Result<LoadedElf, Error> {
    let headers = elf_parse::Elf64Headers::from_vmo(&vmo).context("ELF parsing failed")?;
    let elf_info = elf_load::loaded_elf_info(&headers);
    // Allocate a vmar of the correct size, get the random location, then immediately destroy it.
    // This randomizes the load address without loading into a sub-vmar and breaking mprotect.
    // This is different from how Linux actually lays out the address space. We might need to
    // rewrite it eventually.
    let (temp_vmar, base) = vmar
        .allocate(0, elf_info.high - elf_info.low, zx::VmarFlags::empty())
        .context("Couldn't allocate temporary VMAR")?;
    unsafe { temp_vmar.destroy()? }; // Not unsafe, the vmar is not in the current process
    let bias = base.wrapping_sub(elf_info.low);
    elf_load::map_elf_segments(&vmo, &headers, &vmar, vmar.info()?.base, bias)
        .context("map_elf_segments failed")?;
    Ok(LoadedElf { headers, base, bias })
}

// TODO(tbodt): change to return an errno when it's time to implement execve
// TODO(tbodt): passing the root to this function doesn't make any sense
pub async fn load_executable(
    kernel: &Arc<Kernel>,
    executable: zx::Vmo,
    params: &ProcessParameters,
    root: fio::DirectoryProxy,
) -> Result<TaskOwner, Error> {
    let creds = Credentials { uid: 3, gid: 3, euid: 3, egid: 3 };
    let fs = Arc::new(FileSystem::new(root));
    let task_owner = Task::new(&kernel, &params.name, fs.clone(), creds)?;
    let task = &task_owner.task;

    let main_elf = load_elf(&executable, &task.mm.root_vmar).context("Main ELF failed to load")?;
    let interp_elf = if let Some(interp_hdr) =
        main_elf.headers.program_header_with_type(elf_parse::SegmentType::Interp)?
    {
        let mut interp = vec![0; interp_hdr.filesz as usize];
        executable.read(&mut interp, interp_hdr.offset as u64)?;
        // TODO: once it exists, use the Starnix VFS to open and map the interpreter
        let mut interp = CStr::from_bytes_with_nul(&interp)?.to_str()?;
        if interp.starts_with('/') {
            interp = &interp[1..];
        }
        let interp_vmo = library_loader::load_vmo(&fs.root, interp).await?;
        Some(load_elf(&interp_vmo, &task.mm.root_vmar).context("Interpreter ELF failed to load")?)
    } else {
        None
    };

    let entry_elf = (&interp_elf).as_ref().unwrap_or(&main_elf);
    let entry = entry_elf.headers.file_header().entry.wrapping_add(entry_elf.bias);

    // TODO(tbodt): implement MAP_GROWSDOWN and then reset this to 1 page. The current value of
    // this is based on adding 0x1000 each time a segfault appears.
    let stack_size: usize = 0x5000;
    let stack_vmo = zx::Vmo::create(stack_size as u64)?;
    stack_vmo.set_name(CStr::from_bytes_with_nul(b"[stack]\0")?)?;
    let stack_base = task
        .mm
        .root_vmar
        .map(0, &stack_vmo, 0, stack_size, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE)
        .context("failed to map stack")?;
    let stack = stack_base + stack_size - 8;

    // TODO(tbodt): this would fit better elsewhere
    let stdio = SyslogFile::new();
    assert!(task.files.install_fd(stdio.clone()).unwrap().raw() == 0);
    assert!(task.files.install_fd(stdio.clone()).unwrap().raw() == 1);
    assert!(task.files.install_fd(stdio).unwrap().raw() == 2);

    let auxv = vec![
        (AT_UID, task.creds.uid as u64),
        (AT_EUID, task.creds.euid as u64),
        (AT_GID, task.creds.gid as u64),
        (AT_EGID, task.creds.egid as u64),
        (AT_BASE, interp_elf.map_or(0, |interp| interp.base as u64)),
        (AT_PAGESZ, *PAGE_SIZE),
        (AT_PHDR, main_elf.bias.wrapping_add(main_elf.headers.file_header().phoff) as u64),
        (AT_PHNUM, main_elf.headers.file_header().phnum as u64),
        (AT_ENTRY, main_elf.bias.wrapping_add(main_elf.headers.file_header().entry) as u64),
        (AT_SECURE, 0),
    ];
    let stack = populate_initial_stack(&stack_vmo, &params, auxv, stack_base, stack)?;

    task.thread_group.process.start(&task.thread, entry, stack, zx::Handle::invalid(), 0)?;

    Ok(task_owner)
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    #[fasync::run_singlethreaded(test)]
    async fn trivial_initial_stack() {
        let stack_vmo = zx::Vmo::create(0x4000).expect("VMO creation should succeed.");
        let stack_base: usize = 0x3000_0000;
        let original_stack_start_addr: usize = 0x3000_1000;

        let params = ProcessParameters {
            name: CString::new("trivial").unwrap(),
            argv: vec![],
            environ: vec![],
        };

        let stack_start_addr = populate_initial_stack(
            &stack_vmo,
            &params,
            vec![],
            stack_base,
            original_stack_start_addr,
        )
        .expect("Populate initial stack should succeed.");

        let argc_size: usize = 8;
        let argv_terminator_size: usize = 8;
        let environ_terminator_size: usize = 8;
        let aux_random: usize = 16;
        let aux_null: usize = 16;
        let random_seed: usize = 16;

        let mut payload_size = argc_size
            + argv_terminator_size
            + environ_terminator_size
            + aux_random
            + aux_null
            + random_seed;
        payload_size += payload_size % 16;

        assert_eq!(stack_start_addr, original_stack_start_addr - payload_size);
    }
}
