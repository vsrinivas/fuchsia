#[repr(C)]
pub struct {protocol_name}_ops_t {{
{protocol_fns}
}}

#[repr(C)]
pub struct {protocol_name}Protocol {{
    ops: *mut {protocol_name}_ops_t,
    ctx: *mut u8,
}}

impl Default for {protocol_name}Protocol {{
    fn default() -> Self {{
        {protocol_name}Protocol {{
            ops: core::ptr::null_mut(),
            ctx: core::ptr::null_mut(),
        }}
    }}
}}

impl {protocol_name}Protocol {{
    pub fn from_device<Ctx>(parent_device: &ddk::Device<Ctx>) -> Result<Self, zircon::Status> {{
        let mut ret = Self::default();
        unsafe {{
            let resp = ddk::sys::device_get_protocol(
                parent_device.get_ptr(),
                ddk::sys::ZX_PROTOCOL_{protocol_name_upper},
                &mut ret as *mut _ as *mut libc::c_void);
            zircon::Status::ok(resp).map(|_| ret)
        }}
    }}

{safe_protocol_fns}
}}

/// TODO(bwb): document why this is safe
unsafe impl Send for {protocol_name}Protocol {{ }}
unsafe impl Sync for {protocol_name}Protocol {{ }}

// TODO(bwb): maybe?
// impl Protocol for {protocol_name}Protocol {{ }}
