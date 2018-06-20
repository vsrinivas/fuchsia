extern crate fuchsia_zircon as zx;

// Re-export libc to be used from the c_char macro
#[doc(hidden)]
pub extern crate libc as __libc_reexport;
use __libc_reexport as libc;

use std::ffi::CStr;
use std::marker::PhantomData;
use std::mem;
use std::ptr;

/// `Scope` represents the scope of a trace event.
#[derive(Copy, Clone)]
pub enum Scope {
    Thread,
    Process,
    Global,
}

impl Scope {
    fn into_raw(self) -> sys::trace_scope_t {
        match self {
            Scope::Thread => sys::TRACE_SCOPE_THREAD,
            Scope::Process => sys::TRACE_SCOPE_PROCESS,
            Scope::Global => sys::TRACE_SCOPE_GLOBAL,
        }
    }
}

/// Creates a `&'static CStr` from a string literal.
#[macro_export]
macro_rules! cstr {
    ($s:expr) => (
        // `concat` macro always produces a static string literal.
        // It is always safe to create a CStr from a null-terminated string.
        // If there are interior null bytes, the string will just end early.
        unsafe {
            ::std::ffi::CStr::from_ptr::<'static>(
                concat!($s, "\0").as_ptr() as *const $crate::__libc_reexport::c_char
            )
        }
    )
}

/// Returns true if tracing is enabled.
#[inline]
pub fn is_enabled() -> bool {
    // Trivial no-argument function that will not race
    unsafe {
        sys::trace_is_enabled()
    }
}

/// Returns true if tracing has been enabled for the given category.
pub fn category_enabled(category: &'static CStr) -> bool {
    // Function requires a pointer to a static null-terminated string literal,
    // which `&'static CStr` is.
    unsafe {
        sys::trace_is_category_enabled(category.as_ptr())
    }
}

/// `Arg` holds an argument to a tracing function, which can be one of many types.
#[repr(transparent)]
pub struct Arg<'a>(sys::trace_arg_t, PhantomData<&'a ()>);

/// A trait for types that can be the values of an argument set.
///
/// This trait is not implementable by users of the library.
/// Users should instead use one of the common types which implements
/// `ArgValue`, such as `i32`, `f64`, or `&str`.
pub trait ArgValue {
    fn of<'a>(key: &'a str, value: Self) -> Arg<'a>
        where Self: 'a;
}

// Implements `arg_from` for many types.
// $valname is the name to which to bind the `Self` value in the $value expr
// $ty is the type
// $tag is the union tag indicating the variant of trace_arg_union_t being used
// $value is the union value for that particular type
macro_rules! arg_from {
    ($valname:ident, $(($type:ty, $tag:expr, $value:expr))*) => {
        $(
            impl ArgValue for $type {
                fn of<'a>(key: &'a str, $valname: Self) -> Arg<'a>
                    where Self: 'a
                {
                    #[allow(unused)]
                    let $valname = $valname;

                    Arg(sys::trace_arg_t {
                        name_ref: trace_make_inline_string_ref(key),
                        value: sys::trace_arg_value_t {
                            type_: $tag,
                            value: $value,
                        },
                    }, PhantomData)
                }
            }
        )*
    }
}

// Implement ArgFrom for a variety of types
arg_from!(val,
    ((), sys::TRACE_ARG_NULL, sys::trace_arg_union_t { int32_value: 0 })
    (i32, sys::TRACE_ARG_INT32, sys::trace_arg_union_t { int32_value: val })
    (u32, sys::TRACE_ARG_UINT32, sys::trace_arg_union_t { uint32_value: val })
    (i64, sys::TRACE_ARG_INT64, sys::trace_arg_union_t { int64_value: val })
    (u64, sys::TRACE_ARG_UINT64, sys::trace_arg_union_t { uint64_value: val })
    (f64, sys::TRACE_ARG_DOUBLE, sys::trace_arg_union_t { double_value: val })
);

impl<'a> ArgValue for &'a str {
    fn of<'b>(key: &'b str, val: Self) -> Arg<'b>
        where Self: 'b
    {
        Arg(sys::trace_arg_t {
            name_ref: trace_make_inline_string_ref(key),
            value: sys::trace_arg_value_t {
                type_: sys::TRACE_ARG_STRING,
                value: sys::trace_arg_union_t {
                    string_value_ref: trace_make_inline_string_ref(val),
                }
            },
        }, PhantomData)
    }
}

/// Convenience macro for the `instant` function.
///
/// Example:
///
/// ```rust
/// instant!("foo", "bar", Scope::Process, "x" => 5, "y" => "boo");
/// ```
///
/// is equivalent to
///
/// ```rust
/// instant(cstr!("foo"), cstr!("bar"), Scope::Process,
///     &[ArgValue::of("x", 5), ArgValue::of("y", "boo")]);
/// ```
#[macro_export]
macro_rules! instant {
    ($category:expr, $name:expr, $scope:expr $(, $key:expr => $val:expr)*) => {
        $crate::instant(cstr!($category), cstr!($name), $scope,
            &[$($crate::ArgValue::of($key, $val)),*])
    }
}

/// Writes an instant event representing a single moment in time.
/// The number of `args` must not be greater than 15.
pub fn instant(category: &'static CStr, name: &'static CStr, scope: Scope, args: &[Arg]) {
    assert!(args.len() <= 15, "no more than 15 trace arguments are supported");

    // trace_context_write_xxx functions require that:
    // - category and name are static null-terminated strings (`&'static CStr).
    // - the refs must be valid for the given call
    unsafe {
        let mut category_ref: sys::trace_string_ref_t = mem::uninitialized();
        let context = sys::trace_acquire_context_for_category(category.as_ptr(), &mut category_ref);
        if context != ptr::null() {
            let helper = EventHelper::new(context, name);
            sys::trace_context_write_instant_event_record(
                context,
                helper.ticks,
                &helper.thread_ref,
                &category_ref,
                &helper.name_ref,
                scope.into_raw(),
                args.as_ptr() as *const sys::trace_arg_t,
                args.len()
            );
        }
    }
}

/// Convenience macro for the `counter` function.
///
/// Example:
///
/// ```rust
/// let id = 555;
/// counter!("foo", "bar", id, "x" => 5, "y" => 10);
/// ```
///
/// is equivalent to
///
/// ```rust
/// let id = 555;
/// counter!(cstr!("foo"), cstr!("bar"), id,
///     &[ArgValue::of("x", 5), ArgValue::of("y", 10)]);
/// ```
#[macro_export]
macro_rules! counter {
    ($category:expr, $name:expr, $counter_id:expr $(, $key:expr => $val:expr)*) => {
        $crate::counter(cstr!($category), cstr!($name), $counter_id,
            &[$($crate::ArgValue::of($key, $val)),*])
    }
}

/// Writes a counter event with the specified id.
///
/// The arguments to this event are numeric samples and are typically
/// represented by the visualizer as a stacked area chart. The id serves to
/// distinguish multiple instances of counters which share the same category
/// and name within the same process.
///
/// 1 to 15 numeric arguments can be associated with an event, each of which is
/// interpreted as a distinct time series.
pub fn counter(category: &'static CStr, name: &'static CStr, counter_id: u64, args: &[Arg]) {
    assert!(args.len() >= 1, "trace counter args must include at least one numeric argument");
    assert!(args.len() <= 15, "no more than 15 trace arguments are supported");

    // See unsafety justification in `instant`
    unsafe {
        let mut category_ref: sys::trace_string_ref_t = mem::uninitialized();
        let context = sys::trace_acquire_context_for_category(category.as_ptr(), &mut category_ref);
        if context != ptr::null() {
            let helper = EventHelper::new(context, name);

            sys::trace_context_write_counter_event_record(
                context,
                helper.ticks,
                &helper.thread_ref,
                &category_ref,
                &helper.name_ref,
                counter_id,
                args.as_ptr() as *const sys::trace_arg_t,
                args.len()
            );
        }
    }
}

/// The scope of a duration event, returned by the `duration` function and the `duration!` macro.
/// This type should be `end`ed when the duration completes.
#[derive(Copy, Clone)]
#[must_use = "DurationScope must be `end`ed to be recorded"]
pub struct DurationScope {
    category: &'static CStr,
    name: &'static CStr,
}

/// The scope of a duration event which completes when it goes out of scope or is `drop`ped.
/// Objects of this type can be created from the `end_on_drop` method on `DurationScope`.
pub struct DurationDropScope(DurationScope);

impl Drop for DurationDropScope {
    fn drop(&mut self) {
        self.0.end();
    }
}

impl DurationScope {
    /// Creates a new `DurationDropScope` object which will end the duration
    /// when it goes out of scope or is `drop`ped.
    ///
    /// Example:
    /// 
    /// ```rust
    /// {
    ///     let _scope = duration!("foo", "bar").end_on_drop();
    ///     ...
    ///     // `_scope` will be dropped here and the duration will end.
    /// }
    /// ```
    pub fn end_on_drop(self) -> DurationDropScope {
        DurationDropScope(self)
    }

    /// Ends the duration and records the result.
    /// This function is equivalent to `drop`ping the `DurationScope`.
    pub fn end(self) {
        // See unsafety justification in `instant`
        unsafe {
            let DurationScope { category, name } = self;
            let mut category_ref: sys::trace_string_ref_t = mem::uninitialized();
            let context = sys::trace_acquire_context_for_category(category.as_ptr(), &mut category_ref);
            if context != ptr::null() {
                let helper = EventHelper::new(context, name);

                sys::trace_context_write_duration_begin_event_record(
                    context,
                    helper.ticks,
                    &helper.thread_ref,
                    &category_ref,
                    &helper.name_ref,
                    ptr::null(),
                    0
                );
            }
        }
    }
}

/// Convenience macro for the `duration` function.
///
/// Example:
///
/// ```rust
/// let dur_scope = duration!("foo", "bar", "x" => 5, "y" => 10);
/// ...
/// ...
/// dur_scope.end();
/// ```
///
/// is equivalent to
///
/// ```rust
/// let dur_scope = duration(cstr!("foo"), cstr!("bar"),
///                     &[ArgValue::of("x", 5), ArgValue::of("y", 10)]);
/// ...
/// ...
/// dur_scope.end();
/// ```
#[macro_export]
macro_rules! duration {
    ($category:expr, $name:expr $(, $key:expr => $val:expr)*) => {
        $crate::duration(cstr!($category), cstr!($name),
            &[$($crate::ArgValue::of($key, $val)),*])
    }
}

// Starts a duration event which ends and is recorded when the resulting `DurationScope` is
// dropped.
//
// 0 to 15 arguments can be associated with this duration.
pub fn duration(category: &'static CStr, name: &'static CStr, args: &[Arg]) -> DurationScope {
    assert!(args.len() <= 15, "no more than 15 trace arguments are supported");
    // See justification in `instant`
    unsafe {
        let mut category_ref: sys::trace_string_ref_t = mem::uninitialized();
        let context = sys::trace_acquire_context_for_category(category.as_ptr(), &mut category_ref);
        if context != ptr::null() {
            let helper = EventHelper::new(context, name);

            sys::trace_context_write_duration_begin_event_record(
                context,
                helper.ticks,
                &helper.thread_ref,
                &category_ref,
                &helper.name_ref,
                args.as_ptr() as *const sys::trace_arg_t,
                args.len()
            );
        }
    }

    DurationScope { category, name }
}

struct EventHelper {
    ticks: sys::trace_ticks_t,
    thread_ref: sys::trace_thread_ref_t,
    name_ref: sys::trace_string_ref_t,
}

impl EventHelper {
    // Requires valid ptr to `trace_context_t`
    unsafe fn new(context: *const sys::trace_context_t, name: &'static CStr) -> Self {
        let ticks = zx::ticks_get();

        let mut thread_ref: sys::trace_thread_ref_t = mem::uninitialized();
        sys::trace_context_register_current_thread(context, &mut thread_ref);

        let mut name_ref: sys::trace_string_ref_t = mem::uninitialized();
        sys::trace_context_register_string_literal(context, name.as_ptr(), &mut name_ref);

        EventHelper { ticks, thread_ref, name_ref }
    }
}

// translated from trace-engine/types.h for inlining
fn trace_make_empty_string_ref() -> sys::trace_string_ref_t {
    sys::trace_string_ref_t {
        encoded_value: sys::TRACE_ENCODED_STRING_REF_EMPTY,
        inline_string: ptr::null(),
    }
}

fn trim_to_last_char_boundary(string: &str, max_len: usize) -> &[u8] {
    let mut len = string.len();
    if string.len() > max_len {
        // Trim to the last unicode character that fits within the max length.
        // We search for the last character boundary that is immediately followed
        // by another character boundary (end followed by beginning).
        len = max_len;
        while len > 0 {
            if string.is_char_boundary(len - 1) &&
                string.is_char_boundary(len)
            {
                break;
            }
            len -= 1;
        }
    }
    &string.as_bytes()[0..len]
}

// translated from trace-engine/types.h for inlining
// The resulting `trace_string_ref_t` only lives as long as the input `string`.
fn trace_make_inline_string_ref(string: &str) -> sys::trace_string_ref_t {
    let mut len = string.len() as u32;
    if len == 0 {
        return trace_make_empty_string_ref();
    }

    let string = trim_to_last_char_boundary(
        string, sys::TRACE_ENCODED_STRING_REF_MAX_LENGTH as usize);

    sys::trace_string_ref_t {
        encoded_value: sys::TRACE_ENCODED_STRING_REF_INLINE_FLAG | len,
        inline_string: string.as_ptr() as *const libc::c_char,
    }
}

mod sys {
    #![allow(non_camel_case_types, unused)]
    use zx::sys::{zx_koid_t, zx_obj_type_t, zx_handle_t, zx_status_t};
    use libc;

    pub type trace_ticks_t = u64;
    pub type trace_counter_id_t = u64;
    pub type trace_async_id_t = u64;
    pub type trace_flow_id_t = u64;
    pub type trace_thread_state_t = u32;
    pub type trace_cpu_number_t = u32;
    pub type trace_string_index_t = u32;
    pub type trace_thread_index_t = u32;
    pub type trace_context_t = libc::c_void;

    pub type trace_encoded_string_ref_t = u32;
    pub const TRACE_ENCODED_STRING_REF_EMPTY: trace_encoded_string_ref_t = 0;
    pub const TRACE_ENCODED_STRING_REF_INLINE_FLAG: trace_encoded_string_ref_t = 0x8000;
    pub const TRACE_ENCODED_STRING_REF_LENGTH_MASK: trace_encoded_string_ref_t = 0x7fff;
    pub const TRACE_ENCODED_STRING_REF_MAX_LENGTH: trace_encoded_string_ref_t = 32000;
    pub const TRACE_ENCODED_STRING_REF_MIN_INDEX: trace_encoded_string_ref_t = 0x1;
    pub const TRACE_ENCODED_STRING_REF_MAX_INDEX: trace_encoded_string_ref_t = 0x7fff;

    pub type trace_encoded_thread_ref_t = u32;
    pub const TRACE_ENCODED_THREAD_REF_INLINE: trace_encoded_thread_ref_t = 0;
    pub const TRACE_ENCODED_THREAD_MIN_INDEX: trace_encoded_thread_ref_t = 0x01;
    pub const TRACE_ENCODED_THREAD_MAX_INDEX: trace_encoded_thread_ref_t = 0xff;

    pub type trace_state_t = libc::c_int;
    pub const TRACE_STOPPED: trace_state_t = 0;
    pub const TRACE_STARTED: trace_state_t = 1;
    pub const TRACE_STOPPING: trace_state_t = 2;

    pub type trace_scope_t = libc::c_int;
    pub const TRACE_SCOPE_THREAD: trace_scope_t = 0;
    pub const TRACE_SCOPE_PROCESS: trace_scope_t = 1;
    pub const TRACE_SCOPE_GLOBAL: trace_scope_t = 2;

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct trace_string_ref_t {
        pub encoded_value: trace_encoded_string_ref_t,
        pub inline_string: *const libc::c_char,
    }

    #[repr(C)]
    pub struct trace_thread_ref_t {
        pub encoded_value: trace_encoded_thread_ref_t,
        pub inline_process_koid: zx_koid_t,
        pub inline_thread_koid: zx_koid_t,
    }

    #[repr(C)]
    pub struct trace_arg_t {
        pub name_ref: trace_string_ref_t,
        pub value: trace_arg_value_t,
    }

    #[repr(C)]
    pub union trace_arg_union_t {
        pub int32_value: libc::int32_t,
        pub uint32_value: libc::uint32_t,
        pub int64_value: libc::int64_t,
        pub uint64_value: libc::uint64_t,
        pub double_value: libc::c_double,
        pub string_value_ref: trace_string_ref_t,
        pub pointer_value: libc::uintptr_t,
        pub koid_value: zx_koid_t,
        pub reserved_for_future_expansion: [libc::uintptr_t; 2],
    }

    pub type trace_arg_type_t = libc::c_int;
    pub const TRACE_ARG_NULL: trace_arg_type_t = 0;
    pub const TRACE_ARG_INT32: trace_arg_type_t = 1;
    pub const TRACE_ARG_UINT32: trace_arg_type_t = 2;
    pub const TRACE_ARG_INT64: trace_arg_type_t = 3;
    pub const TRACE_ARG_UINT64: trace_arg_type_t = 4;
    pub const TRACE_ARG_DOUBLE: trace_arg_type_t = 5;
    pub const TRACE_ARG_STRING: trace_arg_type_t = 6;
    pub const TRACE_ARG_POINTER: trace_arg_type_t = 7;
    pub const TRACE_ARG_KOID: trace_arg_type_t = 8;

    #[repr(C)]
    pub struct trace_arg_value_t {
        pub type_: trace_arg_type_t,
        pub value: trace_arg_union_t,
    }

    #[repr(C)]
    pub struct trace_handler_ops_t {
        pub is_category_enabled:
            unsafe fn(handler: *const trace_handler_t, category: *const libc::c_char) -> bool,
        pub trace_started:
            unsafe fn(handler: *const trace_handler_t),
        pub trace_stopped:
            unsafe fn(handler: *const trace_handler_t,
                      async: *const (),//async_t,
                      disposition: zx_status_t,
                      buffer_bytes_written: libc::size_t),
        pub buffer_overflow:
            unsafe fn(handler: *const trace_handler_t),
    }

    #[repr(C)]
    pub struct trace_handler_t {
        pub ops: *const trace_handler_ops_t,
    }

    #[link(name = "trace-engine")]
    extern "C" {
        // From trace-engine/context.h

        pub fn trace_context_is_category_enabled(
            context: *const trace_context_t,
            category_literal: *const libc::c_char) -> bool;

        pub fn trace_context_register_string_copy(
            context: *const trace_context_t,
            string: *const libc::c_char,
            length: libc::size_t,
            out_ref: *mut trace_string_ref_t);

        pub fn trace_context_register_string_literal(
            context: *const trace_context_t,
            string_literal: *const libc::c_char,
            out_ref: *mut trace_string_ref_t);

        pub fn trace_context_register_category_literal(
            context: *const trace_context_t,
            category_literal: *const libc::c_char,
            out_ref: *mut trace_string_ref_t) -> bool;

        pub fn trace_context_register_current_thread(
            context: *const trace_context_t,
            out_ref: *mut trace_thread_ref_t);
        
        pub fn trace_context_register_thread(
            context: *const trace_context_t,
            process_koid: zx_koid_t,
            thread_koid: zx_koid_t,
            out_ref: *mut trace_thread_ref_t);

        pub fn trace_context_write_kernel_object_record(
            context: *const trace_context_t,
            koid: zx_koid_t,
            type_: zx_obj_type_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_kernel_object_record_for_handle(
            context: *const trace_context_t,
            handle: zx_handle_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_process_info_record(
            context: *const trace_context_t,
            process_koid: zx_koid_t,
            process_name_ref: *const trace_string_ref_t);

        pub fn trace_context_write_thread_info_record(
            context: *const trace_context_t,
            process_koid: zx_koid_t,
            thread_koid: zx_koid_t,
            thread_name_ref: *const trace_string_ref_t);

        pub fn trace_context_write_context_switch_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            cpu_number: trace_cpu_number_t,
            outgoing_thread_state: trace_thread_state_t,
            outgoing_thread_ref: *const trace_thread_ref_t,
            incoming_thread_ref: *const trace_thread_ref_t);

        pub fn trace_context_write_log_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            log_message: *const libc::c_char,
            log_message_length: libc::size_t);

        pub fn trace_context_write_instant_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            scope: trace_scope_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_counter_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            counter_id: trace_counter_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_duration_event_record(
            context: *const trace_context_t,
            start_time: trace_ticks_t,
            end_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_duration_begin_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_duration_end_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_async_begin_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            async_id: trace_async_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_async_instant_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            async_id: trace_async_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_async_end_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            async_id: trace_async_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_flow_begin_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            flow_id: trace_flow_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_flow_step_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            flow_id: trace_flow_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_flow_end_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            flow_id: trace_flow_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t);

        pub fn trace_context_write_initialization_record(
            context: *const trace_context_t,
            ticks_per_second: u64);

        pub fn trace_context_write_string_record(
            context: *const trace_context_t,
            index: trace_string_index_t,
            string: *const libc::c_char,
            length: libc::size_t);

        pub fn trace_context_write_thread_record(
            context: *const trace_context_t,
            index: trace_thread_index_t,
            procss_koid: zx_koid_t,
            thread_koid: zx_koid_t);

        pub fn trace_context_alloc_record(
            context: *const trace_context_t,
            num_bytes: libc::size_t) -> *const libc::c_void;

        // From trace-engine/handler.h
        /*
        pub fn trace_start_engine(
            async: *const async_t,
            handler: *const trace_handler_t,
            buffer: *const (),
            buffer_num_bytes: libc::size_t) -> zx_status_t;
            */

        pub fn trace_stop_engine(
            disposition: zx_status_t) -> zx_status_t;

        // From trace-engine/instrumentation.h

        pub fn trace_generate_nonce() -> u64;

        pub fn trace_state() -> trace_state_t;

        pub fn trace_is_enabled() -> bool;

        pub fn trace_is_category_enabled(
            category_literal: *const libc::c_char) -> bool;

        pub fn trace_acquire_context() -> *const trace_context_t;

        pub fn trace_acquire_context_for_category(
            category_literal: *const libc::c_char,
            out_ref: *mut trace_string_ref_t) -> *const trace_context_t;

        pub fn trace_release_context(context: *const trace_context_t);

        pub fn trace_register_observer(event: zx_handle_t) -> zx_status_t;

        pub fn trace_unregister_observer(event: zx_handle_t) -> zx_status_t;

        pub fn trace_notify_observer_updated(event: zx_handle_t);
    }
}

#[cfg(test)]
mod test {
    use {Scope, trim_to_last_char_boundary};

    #[test]
    fn trim_to_last_char_boundary_trims_to_last_character_boundary() {
        assert_eq!(b"x", trim_to_last_char_boundary("x", 5));
        assert_eq!(b"x", trim_to_last_char_boundary("x", 1));
        assert_eq!(b"", trim_to_last_char_boundary("x", 0));
        assert_eq!(b"xxxxx", trim_to_last_char_boundary("xxxxx", 6));
        assert_eq!(b"xxxxx", trim_to_last_char_boundary("xxxxx", 5));
        assert_eq!(b"xxxx", trim_to_last_char_boundary("xxxxx", 4));

        assert_eq!("💩".as_bytes(), trim_to_last_char_boundary("💩", 5));
        assert_eq!("💩".as_bytes(), trim_to_last_char_boundary("💩", 4));
        assert_eq!(b"", trim_to_last_char_boundary("💩", 3));
    }

    #[test]
    fn instant() {
        instant!("foo", "bar", Scope::Process, "x" => 5, "y" => "boo");
    }

    #[test]
    fn counter() {
        let id = 24601;
        counter!("foo", "bar", id, "x" => 5, "y" => 10);
    }

    #[test]
    fn duration() {
        let dur_scope = duration!("foo", "bar", "x" => 5, "y" => 10);
        println!("Between duration creation and duration ending");
        dur_scope.end();
    }
}
