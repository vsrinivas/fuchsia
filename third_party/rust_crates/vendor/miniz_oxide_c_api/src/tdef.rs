use libc::*;
use std::{mem, cmp, ptr, slice};
use std::panic::{catch_unwind, AssertUnwindSafe};

use miniz_oxide::deflate::core::{compress, compress_to_output, create_comp_flags_from_zip_params,
                                 CompressorOxide, TDEFLFlush, TDEFLStatus};

/// Compression callback function type.
pub type PutBufFuncPtrNotNull = unsafe extern "C" fn(
    *const c_void, c_int, *mut c_void) -> bool;
/// `Option` alias for compression callback function type.
pub type PutBufFuncPtr = Option<PutBufFuncPtrNotNull>;

pub struct CallbackFunc {
    pub put_buf_func: PutBufFuncPtrNotNull,
    pub put_buf_user: *mut c_void,
}

/// Deflate flush modes.
pub mod flush_modes {
    use libc::c_int;
    use miniz_oxide::deflate::core::TDEFLFlush;
    pub const MZ_NO_FLUSH: c_int = TDEFLFlush::None as c_int;
    // TODO: This is simply sync flush for now, miniz also treats it as such.
    pub const MZ_PARTIAL_FLUSH: c_int = 1;
    pub const MZ_SYNC_FLUSH: c_int = TDEFLFlush::Sync as c_int;
    pub const MZ_FULL_FLUSH: c_int = TDEFLFlush::Full as c_int;
    pub const MZ_FINISH: c_int = TDEFLFlush::Finish as c_int;
    // TODO: Doesn't seem to be implemented by miniz.
    pub const MZ_BLOCK: c_int = 5;
}

pub mod strategy {
    use libc::c_int;
    use miniz_oxide::deflate::core::CompressionStrategy::*;
    pub const MZ_DEFAULT_STRATEGY: c_int = Default as c_int;
    pub const MZ_FILTERED: c_int = Filtered as c_int;
    pub const MZ_HUFFMAN_ONLY: c_int = HuffmanOnly as c_int;
    pub const MZ_RLE: c_int = RLE as c_int;
    pub const MZ_FIXED: c_int = Fixed as c_int;
}

/// Main compression struct. Not the same as `CompressorOxide`
#[repr(C)]
#[allow(bad_style)]
pub struct tdefl_compressor {
    pub inner: Option<CompressorOxide>,
    pub callback: Option<CallbackFunc>,
}

impl tdefl_compressor {
    pub(crate) fn new(flags: u32) -> Self {
        tdefl_compressor {
            inner: Some(CompressorOxide::new(flags)),
            callback: None,
        }
    }

    pub(crate) fn new_with_callback(flags: u32, func: CallbackFunc) -> Self {
        tdefl_compressor {
            inner: Some(CompressorOxide::new(flags)),
            callback: Some(func),
        }
    }

    /// Sets the inner state to `None` and thus drops it.
    pub fn drop_inner(&mut self) {
        self.inner = None;
    }

    pub fn adler32(&self) -> u32 {
        self.inner.as_ref().map(|i| i.adler32()).unwrap_or(0)
    }

    pub fn prev_return_status(&self) -> TDEFLStatus {
        // Not sure we should return on inner not existing, but that shouldn't happen
        // anyway.
        self.inner.as_ref().map(|i| i.prev_return_status()).unwrap_or(TDEFLStatus::BadParam)
    }

    pub fn flags(&self) -> i32 {
        self.inner.as_ref().map(|i| i.flags()).unwrap_or(0)
    }
}


unmangle!(
pub unsafe extern "C" fn tdefl_compress(
    d: Option<&mut tdefl_compressor>,
    in_buf: *const c_void,
    in_size: Option<&mut usize>,
    out_buf: *mut c_void,
    out_size: Option<&mut usize>,
    flush: TDEFLFlush,
) -> TDEFLStatus {
    match d {
        None => {
            in_size.map(|size| *size = 0);
            out_size.map(|size| *size = 0);
            TDEFLStatus::BadParam
        },
        Some(compressor_wrap) => {
            if let Some(ref mut compressor) = compressor_wrap.inner {
            let in_buf_size = in_size.as_ref().map_or(0, |size| **size);
            let out_buf_size = out_size.as_ref().map_or(0, |size| **size);

            if in_buf_size > 0 && in_buf.is_null() {
                in_size.map(|size| *size = 0);
                out_size.map(|size| *size = 0);
                return TDEFLStatus::BadParam;
            }

            let in_slice = (in_buf as *const u8).as_ref().map_or(&[][..],|in_buf| {
                slice::from_raw_parts(in_buf, in_buf_size)
            });

            let res = match compressor_wrap.callback {
                None => {
                    match (out_buf as *mut u8).as_mut() {
                        Some(out_buf) => compress(
                            compressor,
                            in_slice,
                            slice::from_raw_parts_mut(out_buf, out_buf_size),
                            flush,
                        ),
                        None => {
                            in_size.map(|size| *size = 0);
                            out_size.map(|size| *size = 0);
                            return TDEFLStatus::BadParam;
                        }
                    }
                },
                Some(ref func) => {
                    if out_buf_size > 0 || !out_buf.is_null() {
                        in_size.map(|size| *size = 0);
                        out_size.map(|size| *size = 0);
                        return TDEFLStatus::BadParam;
                    }
                    let res = compress_to_output(
                        compressor,
                        in_slice,
                        flush,
                        |out: &[u8]| {
                            (func.put_buf_func)(
                                &(out[0]) as *const u8 as *const c_void,
                                out.len() as i32,
                                func.put_buf_user
                            )
                        },
                    );
                    (res.0, res.1, 0)
                },
            };

            in_size.map(|size| *size = res.1);
            out_size.map(|size| *size = res.2);
            res.0
            } else {
                TDEFLStatus::BadParam
            }
        }
    }
}

pub unsafe extern "C" fn tdefl_compress_buffer(
    d: Option<&mut tdefl_compressor>,
    in_buf: *const c_void,
    mut in_size: usize,
    flush: TDEFLFlush,
) -> TDEFLStatus {
    tdefl_compress(d, in_buf, Some(&mut in_size), ptr::null_mut(), None, flush)
}

/// Allocate a compressor.
///
/// This does initialize the struct, but not the inner constructor,
/// tdefl_init has to be called before doing anything with it.
pub unsafe extern "C" fn tdefl_allocate() -> *mut tdefl_compressor {
    Box::into_raw(Box::<tdefl_compressor>::new(
        tdefl_compressor {
            inner: None,
            callback: None,
        }
    ))
}

/// Deallocate the compressor. (Does nothing if the argument is null).
///
/// This also calles the compressor's destructor, freeing the internal memory
/// allocated by it.
pub unsafe extern "C" fn tdefl_deallocate(c: *mut tdefl_compressor) {
    if !c.is_null() {
        Box::from_raw(c);
    }
}

/// Initialize the compressor struct in the space pointed to by `d`.
/// if d is null, an error is returned.
///
/// Deinitialization is handled by tdefl_deallocate, and thus
/// tdefl_compressor should not be allocated or freed manually, but only through
/// tdefl_allocate and tdefl_deallocate
pub unsafe extern "C" fn tdefl_init(
    d: Option<&mut tdefl_compressor>,
    put_buf_func: PutBufFuncPtr,
    put_buf_user: *mut c_void,
    flags: c_int,
) -> TDEFLStatus {
    if let Some(d) = d {
        match catch_unwind( AssertUnwindSafe(|| {
        d.inner = Some(CompressorOxide::new(flags as u32));
        if let Some(f) = put_buf_func {
            d.callback = Some(CallbackFunc {
                    put_buf_func: f,
                    put_buf_user: put_buf_user,
            })
        } else {
            d.callback = None;
        };

        })) {
            Ok(_) => TDEFLStatus::Okay,
            Err(_) => {
                eprintln!("FATAL ERROR: Caught panic when initializing the compressor!");
                TDEFLStatus::BadParam
            }
        }
    } else {
        TDEFLStatus::BadParam
    }
}

pub unsafe extern "C" fn tdefl_get_prev_return_status(
    d: Option<&mut tdefl_compressor>,
) -> TDEFLStatus {
    d.map_or(TDEFLStatus::Okay, |d| d.prev_return_status())
}

pub unsafe extern "C" fn tdefl_get_adler32(d: Option<&mut tdefl_compressor>) -> c_uint {
    d.map_or(::MZ_ADLER32_INIT as u32, |d| d.adler32())
}

pub unsafe extern "C" fn tdefl_compress_mem_to_output(
    buf: *const c_void,
    buf_len: usize,
    put_buf_func: PutBufFuncPtr,
    put_buf_user: *mut c_void,
    flags: c_int,
) -> bool {
    if let Some(put_buf_func) = put_buf_func {
        let compressor = ::miniz_def_alloc_func(
            ptr::null_mut(),
            1,
            mem::size_of::<tdefl_compressor>(),
        ) as *mut tdefl_compressor;

        ptr::write(compressor,tdefl_compressor::new_with_callback(flags as u32, CallbackFunc {
            put_buf_func: put_buf_func,
            put_buf_user: put_buf_user,
        }));

        let res = tdefl_compress_buffer(compressor.as_mut(), buf, buf_len, TDEFLFlush::Finish) ==
            TDEFLStatus::Done;
        compressor.as_mut().map(|c| c.drop_inner());
        ::miniz_def_free_func(ptr::null_mut(), compressor as *mut c_void);
        res
    } else {
        false
    }
}
);

struct BufferUser {
    pub size: usize,
    pub capacity: usize,
    pub buf: *mut u8,
    pub expandable: bool,
}

pub unsafe extern "C" fn output_buffer_putter(
    buf: *const c_void,
    len: c_int,
    user: *mut c_void,
) -> bool {
    let user = (user as *mut BufferUser).as_mut();
    match user {
        None => false,
        Some(user) => {
            let new_size = user.size + len as usize;
            if new_size > user.capacity {
                if !user.expandable {
                    return false;
                }
                let mut new_capacity = cmp::max(user.capacity, 128);
                while new_size > new_capacity {
                    new_capacity <<= 1;
                }

                let new_buf = ::miniz_def_realloc_func(
                    ptr::null_mut(),
                    user.buf as *mut c_void,
                    1,
                    new_capacity,
                );

                if new_buf.is_null() {
                    return false;
                }

                user.buf = new_buf as *mut u8;
                user.capacity = new_capacity;
            }

            ptr::copy_nonoverlapping(
                buf as *const u8,
                user.buf.offset(user.size as isize),
                len as usize,
            );
            user.size = new_size;
            true
        }
    }
}

unmangle!(
pub unsafe extern "C" fn tdefl_compress_mem_to_heap(
    src_buf: *const c_void,
    src_buf_len: usize,
    out_len: *mut usize,
    flags: c_int,
) -> *mut c_void {
    match out_len.as_mut() {
        None => ptr::null_mut(),
        Some(len) => {
            *len = 0;

            let mut buffer_user = BufferUser {
                size: 0,
                capacity: 0,
                buf: ptr::null_mut(),
                expandable: true,
            };

            if !tdefl_compress_mem_to_output(
                src_buf,
                src_buf_len,
                Some(output_buffer_putter),
                &mut buffer_user as *mut BufferUser as *mut c_void,
                flags,
            ) {
                ptr::null_mut()
            } else {
                *len = buffer_user.size;
                buffer_user.buf as *mut c_void
            }
        }
    }
}

pub unsafe extern "C" fn tdefl_compress_mem_to_mem(
    out_buf: *mut c_void,
    out_buf_len: usize,
    src_buf: *const c_void,
    src_buf_len: usize,
    flags: c_int,
) -> usize {
    if out_buf.is_null() {
        return 0;
    }
    let mut buffer_user = BufferUser {
        size: 0,
        capacity: out_buf_len,
        buf: out_buf as *mut u8,
        expandable: false,
    };

    if tdefl_compress_mem_to_output(
        src_buf,
        src_buf_len,
        Some(output_buffer_putter),
        &mut buffer_user as *mut BufferUser as *mut c_void,
        flags,
    )
    {
        buffer_user.size
    } else {
        0
    }
}

pub extern "C" fn tdefl_create_comp_flags_from_zip_params(
    level: c_int,
    window_bits: c_int,
    strategy: c_int,
) -> c_uint {
    create_comp_flags_from_zip_params(level, window_bits, strategy)
}
);

#[cfg(test)]
mod test {
    use super::*;
    use miniz_oxide::inflate::decompress_to_vec;

    #[test]
    fn mem_to_heap() {
        let data = b"blargharghawrf31086t13qa9pt7gnseatgawe78vtb6p71v";
        let mut out_len = 0;
        let data_len = data.len();
        let out_data = unsafe {
            let res = tdefl_compress_mem_to_heap(
                data.as_ptr() as *const c_void, data_len, &mut out_len, 0);
            assert!(!res.is_null());
            res
        };
        {
            let out_slice = unsafe {
                slice::from_raw_parts(out_data as *const u8, out_len)
            };
            let dec = decompress_to_vec(out_slice).unwrap();
            assert!(dec.as_slice() == &data[..]);
        }
   }
}
