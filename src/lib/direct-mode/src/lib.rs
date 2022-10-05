// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    fidl::{endpoints::Proxy, AsHandleRef},
    fidl_fuchsia_ldsvc::LoaderProxy,
    fidl_fuchsia_process::{ResolverMarker, MAX_RESOLVE_NAME_SIZE},
    fuchsia_async::Channel as AsyncChannel,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_runtime::{
        duplicate_utc_clock_handle, job_default, process_self, thread_self, vmar_root_self,
        HandleInfo, HandleType,
    },
    fuchsia_zircon::{
        sys::{
            zx_handle_t, zx_vcpu_state_t, zx_vmar_unmap_handle_close_thread_exit, ZX_HANDLE_INVALID,
        },
        system_get_page_size, Channel, Guest, Handle, HandleBased,
        PacketContents::GuestVcpu,
        Rights, Status, Thread, Unowned, Vcpu, VcpuContents, Vmar, VmarFlags, Vmo,
    },
    process_builder::{
        calculate_initial_linker_stack_size, compute_initial_stack_pointer,
        elf_load::load_elf,
        elf_parse::{Elf64Headers, SegmentType},
        get_dynamic_linker, Message, MessageContents, ProcessBuilderError, StartupHandle,
    },
    std::ffi::CString,
    std::num::TryFromIntError,
    std::str::from_utf8,
};

/// A direct mode process loaded by the `ProcessLoader`.
pub struct Process {
    vcpu: Vcpu,
    guest: Guest,
}

impl Process {
    /// Run the initial thread.
    pub fn run(&self) -> Result<()> {
        run_thread(&self.vcpu, &self.guest, &vmar_root_self())?;
        Ok(())
    }
}

/// An ELF process loader for direct mode.
///
/// This type is used to setup the dependencies of an ELF binary, so that we can
/// then load it into direct mode for execution.
pub struct ProcessLoader {
    guest: Guest,
    guest_vmar: Vmar,
    vdso: Vmo,
    bin: Vmo,
    ld: Vmo,
    loader: Channel,
    args: Vec<CString>,
    vars: Vec<CString>,
    paths: Vec<CString>,
    handles: Vec<StartupHandle>,
}

impl ProcessLoader {
    /// Create a new `ProcessLoader`.
    ///
    /// The `guest` and `guest_vmar` must be from a call to `Guest::direct`.
    pub fn new(guest: Guest, guest_vmar: Vmar) -> ProcessLoader {
        ProcessLoader {
            guest,
            guest_vmar,
            vdso: Handle::invalid().into(),
            bin: Handle::invalid().into(),
            ld: Handle::invalid().into(),
            loader: Handle::invalid().into(),
            args: vec![],
            vars: vec![],
            paths: vec![],
            handles: vec![],
        }
    }

    /// Provide the vDSO VMO for the process.
    ///
    /// This must be the "vdso/direct" vDSO.
    pub fn vdso(mut self, vdso: Vmo) -> Result<Self> {
        let name = vdso.get_name()?.into_string()?;
        if name != "vdso/direct" {
            Err(anyhow!("Unexpected vDSO VMO: {}", name))
        } else {
            self.vdso = vdso;
            Ok(self)
        }
    }

    /// Provide the `bin` VMO for the process.
    ///
    /// This is the ELF binary to load.
    pub fn bin(mut self, bin: Vmo) -> Self {
        self.bin = bin;
        self
    }

    /// Provide the `ld` VMO for the process.
    ///
    /// This is the linker binary to load, which will act as the dynamic linker
    /// for the ELF binary.
    pub fn ld(mut self, ld: Vmo) -> Self {
        self.ld = ld;
        self
    }

    /// Provide the `loader` channel for the process.
    ///
    /// This is the loader service use by the dynamic linker to acquire the
    /// additional shared libraries the ELF binary needs.
    pub fn loader(mut self, loader: Channel) -> Self {
        self.loader = loader;
        self
    }

    /// Provide the arguments for the process.
    pub fn args(mut self, args: Vec<CString>) -> Self {
        self.args = args;
        self
    }

    /// Provide the environment variables for the process.
    pub fn vars(mut self, vars: Vec<CString>) -> Self {
        self.vars = vars;
        self
    }

    /// Provide the namespace paths for the process.
    ///
    /// These paths must match handles within the provided startup handles. The
    /// handles must have the type `NamespaceDirectory`, and occur in the same
    /// order as they do in `paths`.
    pub fn paths(mut self, paths: Vec<CString>) -> Self {
        self.paths = paths;
        self
    }

    /// Provide the startup handles for the process.
    pub fn handles(mut self, handles: Vec<StartupHandle>) -> Self {
        self.handles = handles;
        self
    }

    /// Load the process and return a `Process` containing the initial thread.
    pub fn load(mut self) -> Result<Process> {
        // Setup the linker.
        let ld_headers = Elf64Headers::from_vmo(&self.ld)?;
        let ld_elf = load_elf(&self.ld, &ld_headers, &self.guest_vmar)?;
        let mut ld_msg_contents = MessageContents {
            args: self.args.clone(),
            environment_vars: self.vars.clone(),
            namespace_paths: vec![],
            handles: vec![
                StartupHandle {
                    handle: self.loader.into_handle(),
                    info: HandleInfo::new(HandleType::LdsvcLoader, 0),
                },
                StartupHandle {
                    handle: self.bin.into_handle(),
                    info: HandleInfo::new(HandleType::ExecutableVmo, 0),
                },
                StartupHandle {
                    handle: ld_elf.vmar.into_handle(),
                    info: HandleInfo::new(HandleType::LoadedVmar, 0),
                },
                StartupHandle {
                    handle: self.guest_vmar.duplicate_handle(Rights::SAME_RIGHTS)?.into_handle(),
                    info: HandleInfo::new(HandleType::RootVmar, 0),
                },
                StartupHandle {
                    handle: process_self().duplicate(Rights::SAME_RIGHTS)?.into_handle(),
                    info: HandleInfo::new(HandleType::ProcessSelf, 0),
                },
                StartupHandle {
                    handle: thread_self().duplicate(Rights::SAME_RIGHTS)?.into_handle(),
                    info: HandleInfo::new(HandleType::ThreadSelf, 0),
                },
            ],
        };
        let ld_stack_size = calculate_initial_linker_stack_size(&mut ld_msg_contents, 0)?;
        let ld_msg = Message::build(ld_msg_contents)?;
        let (bootstrap, bootstrap_server) = Channel::create()?;
        ld_msg.write(&bootstrap).map_err(ProcessBuilderError::WriteBootstrapMessage)?;

        let ld_stack_vmo = Vmo::create(ld_stack_size.try_into()?)?;
        ld_stack_vmo.set_name(&CString::new("linker stack")?)?;
        let ld_stack_base = self.guest_vmar.map(
            0,
            &ld_stack_vmo,
            0,
            ld_stack_size,
            VmarFlags::PERM_READ | VmarFlags::PERM_WRITE,
        )?;

        // Parse the vDSO VMO.
        let vdso_headers = Elf64Headers::from_vmo(&self.vdso)?;
        let vdso_elf = load_elf(&self.vdso, &vdso_headers, &self.guest_vmar)?;

        // Setup the binary.
        let mut bin_handles = vec![
            StartupHandle {
                handle: job_default().duplicate(Rights::SAME_RIGHTS)?.into_handle(),
                info: HandleInfo::new(HandleType::DefaultJob, 0),
            },
            StartupHandle {
                handle: process_self().duplicate(Rights::SAME_RIGHTS)?.into_handle(),
                info: HandleInfo::new(HandleType::ProcessSelf, 0),
            },
            StartupHandle {
                handle: thread_self().duplicate(Rights::SAME_RIGHTS)?.into_handle(),
                info: HandleInfo::new(HandleType::ThreadSelf, 0),
            },
            StartupHandle {
                handle: self.guest_vmar.into_handle(),
                info: HandleInfo::new(HandleType::RootVmar, 0),
            },
            StartupHandle {
                handle: self.vdso.into_handle(),
                info: HandleInfo::new(HandleType::VdsoVmo, 0),
            },
            StartupHandle {
                handle: ld_stack_vmo.into_handle(),
                info: HandleInfo::new(HandleType::StackVmo, 0),
            },
            StartupHandle {
                handle: duplicate_utc_clock_handle(Rights::SAME_RIGHTS)?.into_handle(),
                info: HandleInfo::new(HandleType::ClockUtc, 0),
            },
        ];
        bin_handles.append(&mut self.handles);
        let bin_msg = Message::build(MessageContents {
            args: self.args,
            environment_vars: self.vars,
            namespace_paths: self.paths,
            handles: bin_handles,
        })?;
        bin_msg.write(&bootstrap).map_err(ProcessBuilderError::WriteBootstrapMessage)?;

        let vcpu = Vcpu::create(&self.guest, ld_elf.entry)?;
        let stack_pointer =
            compute_initial_stack_pointer(ld_stack_base, ld_stack_size).try_into()?;
        let arg1 = bootstrap_server.into_raw().try_into()?;
        let arg2 = vdso_elf.vmar_base.try_into()?;
        let vcpu_state = load_vcpu_state(stack_pointer, arg1, arg2);
        vcpu.write_state(&vcpu_state)?;

        Ok(Process { vcpu, guest: self.guest })
    }
}

extern "C" {
    fn dl_clone_loader_service(handle: *mut zx_handle_t) -> Status;
}

fn clone_loader_service() -> Result<Channel, Status> {
    // SAFETY: We assume that `dl_clone_loader_service` will return `ZX_OK` if
    // the handle is valid, and if the handle is invalid we rely on `Status::ok`
    // to return an error.
    let handle = unsafe {
        let mut handle = ZX_HANDLE_INVALID;
        Status::ok(dl_clone_loader_service(&mut handle).into_raw())?;
        Handle::from_raw(handle)
    };
    Ok(handle.into())
}

const RESOLVER_PREFIX: &str = "#!resolve ";
const MAX_RESOLVER_LINE: usize = RESOLVER_PREFIX.len() + MAX_RESOLVE_NAME_SIZE as usize;

/// Given an ELF binary, return its linker and loader.
pub async fn get_ld_from_bin(bin: &mut Vmo) -> Result<(Vmo, Channel)> {
    // Check if we need to resolve the binary.
    let mut line: [u8; MAX_RESOLVER_LINE] = [0; MAX_RESOLVER_LINE];
    bin.read(&mut line, 0)?;
    let channel = if line.starts_with(RESOLVER_PREFIX.as_bytes()) {
        let pos = line.iter().position(|&c| c == b'\n').unwrap_or(line.len());
        let name = from_utf8(&line[RESOLVER_PREFIX.len()..pos])?;
        let resolver = connect_to_protocol::<ResolverMarker>()?;
        let (status, bin_vmo, client_end) = resolver.resolve(name).await?;
        Status::ok(status)?;
        *bin = bin_vmo.ok_or(anyhow!("Missing binary"))?;
        client_end.ok_or(anyhow!("Missing loader"))?.into_channel()
    } else {
        clone_loader_service()?
    };

    // Fetch the linker and the loader.
    let loader = LoaderProxy::from_channel(AsyncChannel::from_channel(channel)?);
    let bin_headers = Elf64Headers::from_vmo(bin)?;
    let bin_interp = bin_headers
        .program_header_with_type(SegmentType::Interp)?
        // TODO(fxbug.dev/95763): Support statically linked binaries.
        .ok_or(anyhow!("Statically linked binaries are not supported in direct mode"))?;
    let ld_vmo = get_dynamic_linker(&loader, &bin, bin_interp).await?;
    let channel = loader
        .into_channel()
        .or(Err(anyhow!("Failed to convert proxy into channel")))?
        .into_zx_channel();
    Ok((ld_vmo, channel))
}

struct VcpuArgs<'a> {
    guest: &'a Guest,
    entry: usize,
    stack_pointer: u64,
    arg1: u64,
    arg2: u64,

    root_vmar: &'a Vmar,
    stack_base: usize,
    stack_size: usize,
}

#[cfg(target_arch = "aarch64")]
fn load_vcpu_state(stack_pointer: u64, arg1: u64, arg2: u64) -> zx_vcpu_state_t {
    let mut state = zx_vcpu_state_t::default();
    state.sp = stack_pointer;
    state.x[0] = arg1;
    state.x[1] = arg2;
    state
}

#[cfg(target_arch = "x86_64")]
fn load_vcpu_state(stack_pointer: u64, arg1: u64, arg2: u64) -> zx_vcpu_state_t {
    zx_vcpu_state_t { rsp: stack_pointer, rsi: arg2, rdi: arg1, ..zx_vcpu_state_t::default() }
}

// Do not allocate in this function, as the thread has not been set up by host
// user-space.
fn thread_entry(args: &VcpuArgs<'_>, _arg2: usize) {
    let _res = || -> Result<(), Status> {
        let vcpu = Vcpu::create(args.guest, args.entry)?;
        let vcpu_state = load_vcpu_state(args.stack_pointer, args.arg1, args.arg2);
        vcpu.write_state(&vcpu_state)?;
        run_thread(&vcpu, args.guest, args.root_vmar)
    }();
    unsafe {
        zx_vmar_unmap_handle_close_thread_exit(
            args.root_vmar.raw_handle(),
            args.stack_base,
            args.stack_size,
            // The guest has already closed the thread handle.
            ZX_HANDLE_INVALID,
        );
    }
}

fn invalid_args(_: TryFromIntError) -> Status {
    Status::INVALID_ARGS
}

// SAFETY: We assume that `args` points to a valid memory location, and that the
// CPU register we are reading a handle value from points to a valid handle.
#[cfg(target_arch = "aarch64")]
unsafe fn load_thread<'a>(
    args: *mut VcpuArgs<'a>,
    guest: &'a Guest,
    vcpu_state: &'a zx_vcpu_state_t,
    root_vmar: &'a Vmar,
    stack_base: usize,
    stack_size: usize,
) -> Result<Unowned<'a, Thread>, Status> {
    std::ptr::write(
        args,
        VcpuArgs {
            guest,
            entry: vcpu_state.x[1].try_into().map_err(invalid_args)?,
            stack_pointer: vcpu_state.x[2],
            arg1: vcpu_state.x[3],
            arg2: vcpu_state.x[4],
            root_vmar,
            stack_base,
            stack_size,
        },
    );
    let handle = vcpu_state.x[0].try_into().map_err(invalid_args)?;
    Ok(Unowned::<Thread>::from_raw_handle(handle))
}

// SAFETY: We assume that `args` points to a valid memory location, and that the
// CPU register we are reading a handle value from points to a valid handle.
#[cfg(target_arch = "x86_64")]
unsafe fn load_thread<'a>(
    args: *mut VcpuArgs<'a>,
    guest: &'a Guest,
    vcpu_state: &'a zx_vcpu_state_t,
    root_vmar: &'a Vmar,
    stack_base: usize,
    stack_size: usize,
) -> Result<Unowned<'a, Thread>, Status> {
    std::ptr::write(
        args,
        VcpuArgs {
            guest,
            entry: vcpu_state.rsi.try_into().map_err(invalid_args)?,
            stack_pointer: vcpu_state.rdx,
            arg1: vcpu_state.r10,
            arg2: vcpu_state.r8,
            root_vmar,
            stack_base,
            stack_size,
        },
    );
    let handle = vcpu_state.rdi.try_into().map_err(invalid_args)?;
    Ok(Unowned::<Thread>::from_raw_handle(handle))
}

// Note: This function **can not** allocate. We may enter this function from a
// thread that was created within the guest. This means that the allocator was
// not setup for the host. Without that setup, any allocations will fail.
fn run_thread(vcpu: &Vcpu, guest: &Guest, root_vmar: &Vmar) -> Result<(), Status> {
    loop {
        let packet = match vcpu.enter()?.contents() {
            GuestVcpu(packet) => packet,
            _ => return Err(Status::BAD_STATE),
        };
        match packet.contents() {
            VcpuContents::Startup { .. } => {
                let stack_size: usize = system_get_page_size().try_into().map_err(invalid_args)?;
                let reduced_stack_size = stack_size - std::mem::size_of::<VcpuArgs<'_>>();
                let vmo = Vmo::create(stack_size.try_into().map_err(invalid_args)?)?;
                let stack_base = root_vmar.map(
                    0,
                    &vmo,
                    0,
                    stack_size,
                    VmarFlags::PERM_READ | VmarFlags::PERM_WRITE,
                )?;
                let vcpu_state = vcpu.read_state()?;
                // Allocate `VcpuArgs` directly on the thread's stack.
                let args = (stack_base + reduced_stack_size) as *mut VcpuArgs<'_>;
                let thread = unsafe {
                    load_thread(args, guest, &vcpu_state, root_vmar, stack_base, stack_size)?
                };
                thread.start(
                    thread_entry as usize,
                    compute_initial_stack_pointer(stack_base, reduced_stack_size),
                    args as usize,
                    0,
                )?;
            }
            VcpuContents::Exit { .. } => return Ok(()),
            _ => return Err(Status::BAD_STATE),
        };
    }
}
