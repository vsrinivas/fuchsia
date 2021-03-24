// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_ldsvc as fldsvc;
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, AsHandleRef, Status, Task};
use process_builder::{elf_load, elf_parse};
use std::ffi::{CStr, CString};

use crate::executive::*;
use crate::types::*;

pub struct ProcessParameters {
    pub name: CString,
    pub argv: Vec<CString>,
    pub environ: Vec<CString>,
}

fn populate_initial_stack(
    stack_vmo: &zx::Vmo,
    params: &ProcessParameters,
    mut auxv: Vec<(u64, u64)>,
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
        main_data.extend_from_slice(&tag.to_ne_bytes());
        main_data.extend_from_slice(&val.to_ne_bytes());
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
    let (temp_vmar, base) = vmar.allocate(0, elf_info.high - elf_info.low, zx::VmarFlags::empty()).context("Couldn't allocate temporary VMAR")?;
    unsafe { temp_vmar.destroy()? }; // Not unsafe, the vmar is not in the current process
    let bias = base.wrapping_sub(elf_info.low);
    elf_load::map_elf_segments(&vmo, &headers, &vmar, vmar.info()?.base, bias).context("map_elf_segments failed")?;
    Ok(LoadedElf { headers, base, bias })
}

// When it's time to implement execve, this should be changed to return an errno.
pub async fn load_executable(
    job: &zx::Job,
    executable: zx::Vmo,
    loader_service: ClientEnd<fldsvc::LoaderMarker>,
    params: &ProcessParameters,
) -> Result<ProcessContext, Error> {
    let loader_service = loader_service.into_proxy()?;

    job.set_name(&params.name)?;
    let (process, root_vmar) = job.create_child_process(params.name.as_bytes())?;

    let main_elf = load_elf(&executable, &root_vmar).context("Main ELF failed to load")?;
    let interp_elf = if let Some(interp_hdr) =
        main_elf.headers.program_header_with_type(elf_parse::SegmentType::Interp)?
    {
        let mut interp = vec![0; interp_hdr.filesz as usize];
        executable.read(&mut interp, interp_hdr.offset as u64)?;
        let interp = CStr::from_bytes_with_nul(&interp)?.to_str()?;
        let (status, interp_vmo) = loader_service.load_object(interp).await?;
        zx::Status::ok(status)?;
        let interp_vmo = interp_vmo.ok_or_else(|| anyhow!("didn't get a vmo back"))?;
        Some(load_elf(&interp_vmo, &root_vmar).context("Interpreter ELF failed to load")?)
    } else {
        None
    };

    let entry_elf = (&interp_elf).as_ref().unwrap_or(&main_elf);
    let entry = entry_elf.headers.file_header().entry.wrapping_add(entry_elf.bias);

    let stack_size: usize = 0x4000;
    let stack_vmo = zx::Vmo::create(stack_size as u64)?;
    stack_vmo.set_name(CStr::from_bytes_with_nul(b"[stack]\0")?)?;
    let stack_base = root_vmar.map(0, &stack_vmo, 0, stack_size, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE).context("failed to map stack")?;
    let stack = stack_base + stack_size - 8;

    let exceptions = fasync::Channel::from_channel(process.create_exception_channel()?)?;
    let process = ProcessContext {
        handle: process,
        exceptions,
        mm: MemoryManager::new(root_vmar),
        security: SecurityContext { uid: 3, gid: 3, euid: 3, egid: 3 },
    };

    let auxv = vec![
        (AT_UID, process.security.uid as u64),
        (AT_EUID, process.security.euid as u64),
        (AT_GID, process.security.gid as u64),
        (AT_EGID, process.security.egid as u64),
        (AT_BASE, interp_elf.map_or(0, |interp| interp.base as u64)),
        (AT_PHDR, main_elf.bias.wrapping_add(main_elf.headers.file_header().phoff) as u64),
        (AT_PHNUM, main_elf.headers.file_header().phnum as u64),
        (AT_SECURE, 0),
    ];
    let stack = populate_initial_stack(&stack_vmo, &params, auxv, stack_base, stack)?;

    let main_thread = process.handle.create_thread("initial-thread".as_bytes())?;
    process.handle.start(&main_thread, entry, stack, zx::Handle::invalid(), 0)?;

    Ok(process)
}

#[cfg(test)]
mod tests {
    use super::*;

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
