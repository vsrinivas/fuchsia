// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef, HandleBased, Status, Task},
    process_builder::ProcessBuilder,
    std::ffi::CString,
};

use crate::executive::*;
use crate::types::*;

pub struct ProcessParameters {
    pub name: CString,
    pub argv: Vec<CString>,
    pub environ: Vec<CString>,
    pub aux: Vec<u64>,
}

fn populate_initial_stack(
    stack_vmo: &zx::Vmo,
    params: &ProcessParameters,
    stack_base: usize,
    original_stack_start_addr: usize,
) -> Result<usize, Status> {
    let mut string_data_size = 0usize;
    for env in &params.environ {
        string_data_size += env.as_bytes_with_nul().len();
    }
    for arg in &params.argv {
        string_data_size += arg.as_bytes_with_nul().len();
    }
    string_data_size += string_data_size % 8; // Pad to 8-byte boundary

    let random_seed_size = 2 * std::mem::size_of::<u64>(); // Random seed

    let mut header_size = 0usize;
    header_size += (params.aux.len() + 4) * std::mem::size_of::<u64>(); // +4 for AT_RANDOM and AT_NULL.
    header_size += (params.environ.len() + 1) * std::mem::size_of::<u64>();
    header_size += (params.argv.len() + 1) * std::mem::size_of::<u64>();
    header_size += std::mem::size_of::<u64>(); // argc

    let stack_start_addr =
        original_stack_start_addr - header_size - random_seed_size - string_data_size;
    let random_seed_addr = stack_start_addr + header_size;
    let mut next_string_addr = stack_start_addr + header_size + random_seed_size;

    const ZERO: [u8; 8] = [0; 8];
    let mut initial_data = Vec::<u8>::new();

    let argc = params.argv.len();
    initial_data.extend_from_slice(&argc.to_ne_bytes());
    for arg in &params.argv {
        initial_data.extend_from_slice(&next_string_addr.to_ne_bytes());
        next_string_addr += arg.as_bytes_with_nul().len();
    }
    initial_data.extend_from_slice(&ZERO);

    for env in &params.environ {
        initial_data.extend_from_slice(&next_string_addr.to_ne_bytes());
        next_string_addr += env.as_bytes_with_nul().len();
    }

    initial_data.extend_from_slice(&ZERO);

    for val in &params.aux {
        initial_data.extend_from_slice(&val.to_ne_bytes());
    }
    initial_data.extend_from_slice(&AT_RANDOM.to_ne_bytes());
    initial_data.extend_from_slice(&random_seed_addr.to_ne_bytes());
    initial_data.extend_from_slice(&AT_NULL.to_ne_bytes());
    initial_data.extend_from_slice(&ZERO);

    let mut random_seed = [0; 16];
    zx::cprng_draw(&mut random_seed)?;
    initial_data.extend_from_slice(&random_seed);

    for arg in &params.argv {
        initial_data.extend_from_slice(arg.as_bytes_with_nul());
    }
    for env in &params.environ {
        initial_data.extend_from_slice(env.as_bytes_with_nul());
    }

    stack_vmo.write(&initial_data.as_slice(), (stack_start_addr - stack_base) as u64)?;
    return Ok(stack_start_addr);
}

pub async fn load_executable(
    job: &zx::Job,
    executable: zx::Vmo,
    params: &ProcessParameters,
) -> Result<ProcessContext, Error> {
    job.set_name(&params.name)?;

    let builder = ProcessBuilder::new(&params.name, &job, executable)?;
    let mut built = builder.build().await?;

    built.stack = populate_initial_stack(&built.stack_vmo, &params, built.stack_base, built.stack)?;

    let process = ProcessContext {
        handle: built.process.duplicate_handle(zx::Rights::SAME_RIGHTS)?,
        exceptions: fasync::Channel::from_channel(built.process.create_exception_channel()?)?,
        mm: MemoryManager::new(built.root_vmar.duplicate_handle(zx::Rights::SAME_RIGHTS)?),
        security: SecurityContext { uid: 3, gid: 3, euid: 3, egid: 3 },
    };

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
            aux: vec![],
        };

        let stack_start_addr =
            populate_initial_stack(&stack_vmo, &params, stack_base, original_stack_start_addr)
                .expect("Populate initial stack should succeed.");

        let argc_size: usize = 8;
        let argv_terminator_size: usize = 8;
        let environ_terminator_size: usize = 8;
        let aux_random: usize = 16;
        let aux_null: usize = 16;
        let random_seed: usize = 16;

        let payload_size = argc_size
            + argv_terminator_size
            + environ_terminator_size
            + aux_random
            + aux_null
            + random_seed;

        assert_eq!(stack_start_addr, original_stack_start_addr - payload_size);
    }
}
