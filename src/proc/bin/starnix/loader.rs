// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_ldsvc as fldsvc, fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef, HandleBased, Status, Task},
    process_builder::ProcessBuilder,
    std::ffi::CString,
};

use crate::executive::*;
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

pub async fn load_executable(
    job: &zx::Job,
    executable: zx::Vmo,
    loader_service: ClientEnd<fldsvc::LoaderMarker>,
    params: &ProcessParameters,
) -> Result<ProcessContext, Error> {
    job.set_name(&params.name)?;

    let mut builder = ProcessBuilder::new(&params.name, &job, executable)?;
    builder.set_loader_service(loader_service)?;
    builder.set_min_stack_size(0x2000);
    let mut built = builder.build().await?;

    let process = ProcessContext {
        handle: built.process.duplicate_handle(zx::Rights::SAME_RIGHTS)?,
        exceptions: fasync::Channel::from_channel(built.process.create_exception_channel()?)?,
        mm: MemoryManager::new(built.root_vmar.duplicate_handle(zx::Rights::SAME_RIGHTS)?),
        security: SecurityContext { uid: 3, gid: 3, euid: 3, egid: 3 },
    };

    let auxv = vec![
        (AT_UID, process.security.uid as u64),
        (AT_EUID, process.security.euid as u64),
        (AT_GID, process.security.gid as u64),
        (AT_EGID, process.security.egid as u64),
        (AT_PHDR, (built.elf_base + built.elf_headers.file_header().phoff) as u64),
        (AT_PHNUM, built.elf_headers.file_header().phnum as u64),
        (AT_SECURE, 0),
    ];
    built.stack =
        populate_initial_stack(&built.stack_vmo, &params, auxv, built.stack_base, built.stack)?;

    built.start()?;

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
