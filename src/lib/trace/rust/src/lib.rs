// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use cstr::cstr;

use {
    fuchsia_zircon as zx,
    std::{ffi::CStr, marker::PhantomData, mem, ptr},
};

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

/// Returns true if tracing is enabled.
#[inline]
pub fn is_enabled() -> bool {
    // Trivial no-argument function that will not race
    unsafe { sys::trace_state() != sys::TRACE_STOPPED }
}

/// Returns true if tracing has been enabled for the given category.
pub fn category_enabled(category: &'static CStr) -> bool {
    // Function requires a pointer to a static null-terminated string literal,
    // which `&'static CStr` is.
    unsafe { sys::trace_is_category_enabled(category.as_ptr()) }
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
    where
        Self: 'a;
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
#[rustfmt::skip]
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
    where
        Self: 'b,
    {
        Arg(
            sys::trace_arg_t {
                name_ref: trace_make_inline_string_ref(key),
                value: sys::trace_arg_value_t {
                    type_: sys::TRACE_ARG_STRING,
                    value: sys::trace_arg_union_t {
                        string_value_ref: trace_make_inline_string_ref(val),
                    },
                },
            },
            PhantomData,
        )
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
        $crate::instant($crate::cstr!($category), $crate::cstr!($name), $scope,
            &[$($crate::ArgValue::of($key, $val)),*])
    }
}

/// Writes an instant event representing a single moment in time.
/// The number of `args` must not be greater than 15.
pub fn instant(category: &'static CStr, name: &'static CStr, scope: Scope, args: &[Arg<'_>]) {
    assert!(args.len() <= 15, "no more than 15 trace arguments are supported");

    // trace_context_write_xxx functions require that:
    // - category and name are static null-terminated strings (`&'static CStr).
    // - the refs must be valid for the given call
    unsafe {
        let mut category_ref = mem::MaybeUninit::<sys::trace_string_ref_t>::uninit();
        let context =
            sys::trace_acquire_context_for_category(category.as_ptr(), category_ref.as_mut_ptr());
        if context != ptr::null() {
            let helper = EventHelper::new(context, name);
            sys::trace_context_write_instant_event_record(
                context,
                helper.ticks,
                &helper.thread_ref,
                category_ref.as_ptr(),
                &helper.name_ref,
                scope.into_raw(),
                args.as_ptr() as *const sys::trace_arg_t,
                args.len(),
            );
            sys::trace_release_context(context);
        }
    }
}

/// Convenience macro for the `alert` function.
///
/// Example:
///
/// ```rust
/// alert!("foo", "bar");
/// ```
///
/// is equivalent to
///
/// ```rust
/// alert(cstr!("foo"), cstr!("bar"));
/// ```
#[macro_export]
macro_rules! alert {
    ($category:expr, $name:expr) => {
        $crate::alert($crate::cstr!($category), $crate::cstr!($name))
    };
}

/// Sends an alert, which can be mapped to an action.
pub fn alert(category: &'static CStr, name: &'static CStr) {
    // trace_context_write_xxx functions require that:
    // - category and name are static null-terminated strings (`&'static CStr).
    // - the refs must be valid for the given call
    unsafe {
        let mut category_ref = mem::MaybeUninit::<sys::trace_string_ref_t>::uninit();
        let context =
            sys::trace_acquire_context_for_category(category.as_ptr(), category_ref.as_mut_ptr());
        if context != ptr::null() {
            let helper = EventHelper::new(context, name);
            sys::trace_context_send_alert(context, &helper.name_ref);
            sys::trace_release_context(context);
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
/// counter(cstr!("foo"), cstr!("bar"), id,
///     &[ArgValue::of("x", 5), ArgValue::of("y", 10)]);
/// ```
#[macro_export]
macro_rules! counter {
    ($category:expr, $name:expr, $counter_id:expr $(, $key:expr => $val:expr)*) => {
        $crate::counter($crate::cstr!($category), $crate::cstr!($name), $counter_id,
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
pub fn counter(category: &'static CStr, name: &'static CStr, counter_id: u64, args: &[Arg<'_>]) {
    assert!(args.len() >= 1, "trace counter args must include at least one numeric argument");
    assert!(args.len() <= 15, "no more than 15 trace arguments are supported");

    // See unsafety justification in `instant`
    unsafe {
        let mut category_ref = mem::MaybeUninit::<sys::trace_string_ref_t>::uninit();
        let context =
            sys::trace_acquire_context_for_category(category.as_ptr(), category_ref.as_mut_ptr());
        if context != ptr::null() {
            let helper = EventHelper::new(context, name);
            sys::trace_context_write_counter_event_record(
                context,
                helper.ticks,
                &helper.thread_ref,
                category_ref.as_ptr(),
                &helper.name_ref,
                counter_id,
                args.as_ptr() as *const sys::trace_arg_t,
                args.len(),
            );
            sys::trace_release_context(context);
        }
    }
}

/// The scope of a duration event, returned by the `duration` function and the `duration!` macro.
/// The duration will be `end'ed` when this object is dropped.
#[must_use = "DurationScope must be `end`ed to be recorded"]
pub struct DurationScope<'a> {
    category: &'static CStr,
    name: &'static CStr,
    args: &'a [Arg<'a>],
    start_time: sys::trace_ticks_t,
}

impl<'a> DurationScope<'a> {
    /// Starts a new duration scope that starts now and will be end'ed when
    /// this object is dropped.
    pub fn begin(category: &'static CStr, name: &'static CStr, args: &'a [Arg<'_>]) -> Self {
        let start_time = zx::ticks_get();
        Self { category, name, args, start_time }
    }
}

impl<'a> Drop for DurationScope<'a> {
    fn drop(&mut self) {
        // See unsafety justification in `instant`
        unsafe {
            let DurationScope { category, name, args, start_time } = self;
            let mut category_ref = mem::MaybeUninit::<sys::trace_string_ref_t>::uninit();
            let context = sys::trace_acquire_context_for_category(
                category.as_ptr(),
                category_ref.as_mut_ptr(),
            );
            if context != ptr::null() {
                let helper = EventHelper::new(context, name);
                sys::trace_context_write_duration_event_record(
                    context,
                    *start_time,
                    helper.ticks,
                    &helper.thread_ref,
                    category_ref.as_ptr(),
                    &helper.name_ref,
                    args.as_ptr() as *const sys::trace_arg_t,
                    args.len(),
                );
                sys::trace_release_context(context);
            }
        }
    }
}

/// Convenience macro for the `duration` function that can be used to trace
/// the duration of a scope. If you need finer grained control over when a
/// duration starts and stops, see `duration_begin` and `duration_end`.
///
/// Example:
///
/// ```rust
///   {
///       duration!("foo", "bar", "x" => 5, "y" => 10);
///       ...
///       ...
///       // event will be recorded on drop.
///   }
/// ```
///
/// is equivalent to
///
/// ```rust
///   {
///       let args = [ArgValue::of("x", 5), ArgValue::of("y", 10)];
///       let _scope = duration(cstr!("foo"), cstr!("bar"), &args);
///       ...
///       ...
///       // event will be recorded on drop.
///   }
/// ```
#[macro_export]
macro_rules! duration {
    ($category:expr, $name:expr $(, $key:expr => $val:expr)*) => {
        let args = [$($crate::ArgValue::of($key, $val)),*];
        let _scope = $crate::duration($crate::cstr!($category), $crate::cstr!($name), &args);
    }
}

/// Writes a duration event which ends when the current scope exits, or the
/// `end` method is manually called.
///
/// Durations describe work which is happening synchronously on one thread.
/// They can be nested to represent a control flow stack.
///
/// 0 to 15 arguments can be associated with the event, each of which is used
/// to annotate the duration with additional information.
pub fn duration<'a>(
    category: &'static CStr,
    name: &'static CStr,
    args: &'a [Arg<'_>],
) -> DurationScope<'a> {
    assert!(args.len() <= 15, "no more than 15 trace arguments are supported");
    DurationScope::begin(category, name, args)
}

/// Convenience macro for the `duration_begin` function.
///
/// Example:
///
/// ```rust
/// duration_begin!("foo", "bar", "x" => 5, "y" => "boo");
/// ```
///
/// is equivalent to
///
/// ```rust
/// duration_begin(cstr!("foo"), cstr!("bar"),
///     &[ArgValue::of("x", 5), ArgValue::of("y", "boo")]);
/// ```
#[macro_export]
macro_rules! duration_begin {
    ($category:expr, $name:expr $(, $key:expr => $val:expr)*) => {
        $crate::duration_begin($crate::cstr!($category), $crate::cstr!($name),
            &[$($crate::ArgValue::of($key, $val)),*])
    }
}

/// Convenience macro for the `duration_end` function.
///
/// Example:
///
/// ```rust
/// duration_end!("foo", "bar", "x" => 5, "y" => "boo");
/// ```
///
/// is equivalent to
///
/// ```rust
/// duration_end(cstr!("foo"), cstr!("bar"),
///     &[ArgValue::of("x", 5), ArgValue::of("y", "boo")]);
/// ```
#[macro_export]
macro_rules! duration_end {
    ($category:expr, $name:expr $(, $key:expr => $val:expr)*) => {
        $crate::duration_end($crate::cstr!($category), $crate::cstr!($name),
            &[$($crate::ArgValue::of($key, $val)),*])
    }
}

macro_rules! duration_event {
    ($( #[$docs:meta] )* $name:ident, $sys_method:path $(,)*) => {
        $( #[$docs] )*
        pub fn $name(category: &'static CStr, name: &'static CStr, args: &[Arg<'_>]) {
            assert!(args.len() <= 15, "no more than 15 trace arguments are supported");
            // See justification in `instant`
            unsafe {
                let mut category_ref = mem::MaybeUninit::<sys::trace_string_ref_t>::uninit();
                let context = sys::trace_acquire_context_for_category(
                    category.as_ptr(), category_ref.as_mut_ptr()
                );
                if context != ptr::null() {
                    let helper = EventHelper::new(context, name);
                    $sys_method(
                        context,
                        helper.ticks,
                        &helper.thread_ref,
                        category_ref.as_ptr(),
                        &helper.name_ref,
                        args.as_ptr() as *const sys::trace_arg_t,
                        args.len(),
                    );
                    sys::trace_release_context(context);
                }
            }
        }
    };
}
duration_event!(
    /// Writes a duration begin event only.
    /// This event must be matched by a duration end event with the same category and name.
    ///
    /// Durations describe work which is happening synchronously on one thread.
    /// They can be nested to represent a control flow stack.
    ///
    /// 0 to 15 arguments can be associated with the event, each of which is used
    /// to annotate the duration with additional information.  The arguments provided
    /// to matching duration begin and duration end events are combined together in
    /// the trace; it is not necessary to repeat them.
    duration_begin,
    sys::trace_context_write_duration_begin_event_record,
);

duration_event!(
    /// Writes a duration end event only.
    ///
    /// Durations describe work which is happening synchronously on one thread.
    /// They can be nested to represent a control flow stack.
    ///
    /// 0 to 15 arguments can be associated with the event, each of which is used
    /// to annotate the duration with additional information.  The arguments provided
    /// to matching duration begin and duration end events are combined together in
    /// the trace; it is not necessary to repeat them.
    duration_end,
    sys::trace_context_write_duration_end_event_record,
);

#[macro_export]
macro_rules! blob {
    ($category:expr, $name:expr, $bytes:expr $(, $key:expr => $val:expr)*) => {
        $crate::blob_fn($crate::cstr!($category), $crate::cstr!($name), $bytes, &[$($crate::ArgValue::of($key, $val)),*])
    }
}
pub fn blob_fn(category: &'static CStr, name: &'static CStr, bytes: &[u8], args: &[Arg<'_>]) {
    // trace_context_write_xxx functions require that:
    // - category and name are static null-terminated strings (&'static CStr).
    // - the refs must be valid for the given call
    unsafe {
        let mut category_ref = mem::MaybeUninit::<sys::trace_string_ref_t>::uninit();
        let context =
            sys::trace_acquire_context_for_category(category.as_ptr(), category_ref.as_mut_ptr());
        if context != ptr::null() {
            let helper = EventHelper::new(context, name);
            sys::trace_context_write_blob_event_record(
                context,
                helper.ticks,
                &helper.thread_ref,
                category_ref.as_ptr(),
                &helper.name_ref,
                bytes.as_ptr() as *const core::ffi::c_void,
                bytes.len(),
                args.as_ptr() as *const sys::trace_arg_t,
                args.len(),
            );
            sys::trace_release_context(context);
        }
    }
}

/// Convenience macro for the `flow_begin` function.
///
/// Example:
///
/// ```rust
/// let flow_id = 1234;
/// flow_begin!("foo", "bar", flow_id, "x" => 5, "y" => "boo");
/// ```
///
/// is equivalent to
///
/// ```rust
/// flow_begin(cstr!("foo"), cstr!("bar"), flow_id,
///     &[ArgValue::of("x", 5), ArgValue::of("y", "boo")]);
/// ```
#[macro_export]
macro_rules! flow_begin {
    ($category:expr, $name:expr, $flow_id:expr $(, $key:expr => $val:expr)*) => {
        $crate::flow_begin($crate::cstr!($category), $crate::cstr!($name), $flow_id,
            &[$($crate::ArgValue::of($key, $val)),*])
    }
}

/// Convenience macro for the `flow_step` function.
///
/// Example:
///
/// ```rust
/// let flow_id = 1234;
/// flow_step!("foo", "bar", flow_id, "x" => 5, "y" => "boo");
/// ```
///
/// is equivalent to
///
/// ```rust
/// flow_step(cstr!("foo"), cstr!("bar"), flow_id,
///     &[ArgValue::of("x", 5), ArgValue::of("y", "boo")]);
/// ```
#[macro_export]
macro_rules! flow_step {
    ($category:expr, $name:expr, $flow_id:expr $(, $key:expr => $val:expr)*) => {
        $crate::flow_step($crate::cstr!($category), $crate::cstr!($name), $flow_id,
            &[$($crate::ArgValue::of($key, $val)),*])
    }
}

/// Convenience macro for the `flow_end` function.
///
/// Example:
///
/// ```rust
/// let flow_id = 1234;
/// flow_end!("foo", "bar", flow_id, "x" => 5, "y" => "boo");
/// ```
///
/// is equivalent to
///
/// ```rust
/// flow_end(cstr!("foo"), cstr!("bar"), flow_id,
///     &[ArgValue::of("x", 5), ArgValue::of("y", "boo")]);
/// ```
#[macro_export]
macro_rules! flow_end {
    ($category:expr, $name:expr, $flow_id:expr $(, $key:expr => $val:expr)*) => {
        $crate::flow_end($crate::cstr!($category), $crate::cstr!($name), $flow_id,
            &[$($crate::ArgValue::of($key, $val)),*])
    }
}

macro_rules! flow_event {
    ($( #[$docs:meta] )* $name:ident, $sys_method:path$(,)*) => {
        $( #[$docs] )*
        pub fn $name(category: &'static CStr, name: &'static CStr, flow_id: u64, args: &[Arg<'_>]) {
            assert!(args.len() <= 15, "no more than 15 trace arguments are supported");
            // See justification in `instant`
            unsafe {
                let mut category_ref = mem::MaybeUninit::<sys::trace_string_ref_t>::uninit();
                let context = sys::trace_acquire_context_for_category(
                    category.as_ptr(), category_ref.as_mut_ptr()
                );
                if context != ptr::null() {
                    let helper = EventHelper::new(context, name);
                    $sys_method(
                        context,
                        helper.ticks,
                        &helper.thread_ref,
                        category_ref.as_ptr(),
                        &helper.name_ref,
                        flow_id,
                        args.as_ptr() as *const sys::trace_arg_t,
                        args.len(),
                    );
                    sys::trace_release_context(context);
                }
            }
        }
    };
}

flow_event!(
    /// Writes a flow begin event with the specified id.
    /// This event may be followed by flow steps events and must be matched by
    /// a flow end event with the same category, name, and id.
    ///
    /// Flow events describe control flow handoffs between threads or across processes.
    /// They are typically represented as arrows in a visualizer.  Flow arrows are
    /// from the end of the duration event which encloses the beginning of the flow
    /// to the beginning of the duration event which encloses the next step or the
    /// end of the flow.  The id serves to correlate flows which share the same
    /// category and name across processes.
    ///
    /// This event must be enclosed in a duration event which represents where
    /// the flow handoff occurs.
    ///
    /// 0 to 15 arguments can be associated with the event, each of which is used
    /// to annotate the flow with additional information.  The arguments provided
    /// to matching flow begin, flow step, and flow end events are combined together
    /// in the trace; it is not necessary to repeat them.
    flow_begin,
    sys::trace_context_write_flow_begin_event_record,
);

flow_event!(
    /// Writes a flow step event with the specified id.
    ///
    /// Flow events describe control flow handoffs between threads or across processes.
    /// They are typically represented as arrows in a visualizer.  Flow arrows are
    /// from the end of the duration event which encloses the beginning of the flow
    /// to the beginning of the duration event which encloses the next step or the
    /// end of the flow.  The id serves to correlate flows which share the same
    /// category and name across processes.
    ///
    /// This event must be enclosed in a duration event which represents where
    /// the flow handoff occurs.
    ///
    /// 0 to 15 arguments can be associated with the event, each of which is used
    /// to annotate the flow with additional information.  The arguments provided
    /// to matching flow begin, flow step, and flow end events are combined together
    /// in the trace; it is not necessary to repeat them.
    flow_step,
    sys::trace_context_write_flow_step_event_record,
);

flow_event!(
    /// Writes a flow end event with the specified id.
    ///
    /// Flow events describe control flow handoffs between threads or across processes.
    /// They are typically represented as arrows in a visualizer.  Flow arrows are
    /// from the end of the duration event which encloses the beginning of the flow
    /// to the beginning of the duration event which encloses the next step or the
    /// end of the flow.  The id serves to correlate flows which share the same
    /// category and name across processes.
    ///
    /// This event must be enclosed in a duration event which represents where
    /// the flow handoff occurs.
    ///
    /// 0 to 15 arguments can be associated with the event, each of which is used
    /// to annotate the flow with additional information.  The arguments provided
    /// to matching flow begin, flow step, and flow end events are combined together
    /// in the trace; it is not necessary to repeat them.
    flow_end,
    sys::trace_context_write_flow_end_event_record,
);

struct EventHelper {
    ticks: sys::trace_ticks_t,
    thread_ref: sys::trace_thread_ref_t,
    name_ref: sys::trace_string_ref_t,
}

impl EventHelper {
    // Requires valid ptr to `trace_context_t`
    unsafe fn new(context: *const sys::trace_context_t, name: &'static CStr) -> Self {
        let ticks = zx::ticks_get();

        let mut thread_ref = mem::MaybeUninit::<sys::trace_thread_ref_t>::uninit();
        sys::trace_context_register_current_thread(context, thread_ref.as_mut_ptr());
        let thread_ref = thread_ref.assume_init();

        let mut name_ref = mem::MaybeUninit::<sys::trace_string_ref_t>::uninit();
        sys::trace_context_register_string_literal(context, name.as_ptr(), name_ref.as_mut_ptr());
        let name_ref = name_ref.assume_init();

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
            if string.is_char_boundary(len - 1) && string.is_char_boundary(len) {
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
    let len = string.len() as u32;
    if len == 0 {
        return trace_make_empty_string_ref();
    }

    let string =
        trim_to_last_char_boundary(string, sys::TRACE_ENCODED_STRING_REF_MAX_LENGTH as usize);

    sys::trace_string_ref_t {
        encoded_value: sys::TRACE_ENCODED_STRING_REF_INLINE_FLAG | len,
        inline_string: string.as_ptr() as *const libc::c_char,
    }
}

mod sys {
    #![allow(non_camel_case_types, unused)]
    use fuchsia_zircon::sys::{zx_handle_t, zx_koid_t, zx_obj_type_t, zx_status_t, zx_ticks_t};

    pub type trace_ticks_t = zx_ticks_t;
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

    pub type trace_blob_type_t = libc::c_int;
    pub const TRACE_BLOB_TYPE_DATA: trace_blob_type_t = 1;
    pub const TRACE_BLOB_TYPE_LAST_BRANCH: trace_blob_type_t = 2;

    #[repr(C)]
    #[derive(Copy, Clone)]
    pub struct trace_string_ref_t {
        pub encoded_value: trace_encoded_string_ref_t,
        pub inline_string: *const libc::c_char,
    }

    // A trace_string_ref_t object is created from a string slice.
    // The trace_string_ref_t object is contained inside an Arg object.
    // whose lifetime matches the string slice to ensure that the memory
    // cannot be de-allocated during the trace.
    //
    // trace_string_ref_t is safe for Send + Sync because the memory that
    // inline_string points to is guaranteed to be valid throughout the trace.
    //
    // For more information, see the ArgValue implementation for &str in this file.
    unsafe impl Send for trace_string_ref_t {}
    unsafe impl Sync for trace_string_ref_t {}

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
        pub int32_value: i32,
        pub uint32_value: u32,
        pub int64_value: i64,
        pub uint64_value: u64,
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
        pub trace_started: unsafe fn(handler: *const trace_handler_t),
        pub trace_stopped: unsafe fn(
            handler: *const trace_handler_t,
            async_ptr: *const (), //async_t,
            disposition: zx_status_t,
            buffer_bytes_written: libc::size_t,
        ),
        pub buffer_overflow: unsafe fn(handler: *const trace_handler_t),
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
            category_literal: *const libc::c_char,
        ) -> bool;

        pub fn trace_context_register_string_copy(
            context: *const trace_context_t,
            string: *const libc::c_char,
            length: libc::size_t,
            out_ref: *mut trace_string_ref_t,
        );

        pub fn trace_context_register_string_literal(
            context: *const trace_context_t,
            string_literal: *const libc::c_char,
            out_ref: *mut trace_string_ref_t,
        );

        pub fn trace_context_register_category_literal(
            context: *const trace_context_t,
            category_literal: *const libc::c_char,
            out_ref: *mut trace_string_ref_t,
        ) -> bool;

        pub fn trace_context_register_current_thread(
            context: *const trace_context_t,
            out_ref: *mut trace_thread_ref_t,
        );

        pub fn trace_context_register_thread(
            context: *const trace_context_t,
            process_koid: zx_koid_t,
            thread_koid: zx_koid_t,
            out_ref: *mut trace_thread_ref_t,
        );

        pub fn trace_context_write_kernel_object_record(
            context: *const trace_context_t,
            koid: zx_koid_t,
            type_: zx_obj_type_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_kernel_object_record_for_handle(
            context: *const trace_context_t,
            handle: zx_handle_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_process_info_record(
            context: *const trace_context_t,
            process_koid: zx_koid_t,
            process_name_ref: *const trace_string_ref_t,
        );

        pub fn trace_context_write_thread_info_record(
            context: *const trace_context_t,
            process_koid: zx_koid_t,
            thread_koid: zx_koid_t,
            thread_name_ref: *const trace_string_ref_t,
        );

        pub fn trace_context_write_context_switch_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            cpu_number: trace_cpu_number_t,
            outgoing_thread_state: trace_thread_state_t,
            outgoing_thread_ref: *const trace_thread_ref_t,
            incoming_thread_ref: *const trace_thread_ref_t,
        );

        pub fn trace_context_write_log_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            log_message: *const libc::c_char,
            log_message_length: libc::size_t,
        );

        pub fn trace_context_write_instant_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            scope: trace_scope_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_send_alert(
            context: *const trace_context_t,
            name_ref: *const trace_string_ref_t,
        );

        pub fn trace_context_write_counter_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            counter_id: trace_counter_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_duration_event_record(
            context: *const trace_context_t,
            start_time: trace_ticks_t,
            end_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_blob_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            blob: *const libc::c_void,
            blob_size: libc::size_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_duration_begin_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_duration_end_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_async_begin_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            async_id: trace_async_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_async_instant_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            async_id: trace_async_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_async_end_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            async_id: trace_async_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_flow_begin_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            flow_id: trace_flow_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_flow_step_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            flow_id: trace_flow_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_flow_end_event_record(
            context: *const trace_context_t,
            event_time: trace_ticks_t,
            thread_ref: *const trace_thread_ref_t,
            category_ref: *const trace_string_ref_t,
            name_ref: *const trace_string_ref_t,
            flow_id: trace_flow_id_t,
            args: *const trace_arg_t,
            num_args: libc::size_t,
        );

        pub fn trace_context_write_initialization_record(
            context: *const trace_context_t,
            ticks_per_second: u64,
        );

        pub fn trace_context_write_string_record(
            context: *const trace_context_t,
            index: trace_string_index_t,
            string: *const libc::c_char,
            length: libc::size_t,
        );

        pub fn trace_context_write_thread_record(
            context: *const trace_context_t,
            index: trace_thread_index_t,
            procss_koid: zx_koid_t,
            thread_koid: zx_koid_t,
        );

        pub fn trace_context_alloc_record(
            context: *const trace_context_t,
            num_bytes: libc::size_t,
        ) -> *const libc::c_void;

        // From trace-engine/handler.h
        /*
        pub fn trace_start_engine(
            async_ptr: *const async_t,
            handler: *const trace_handler_t,
            buffer: *const (),
            buffer_num_bytes: libc::size_t) -> zx_status_t;
            */

        pub fn trace_stop_engine(disposition: zx_status_t) -> zx_status_t;

        // From trace-engine/instrumentation.h

        pub fn trace_generate_nonce() -> u64;

        pub fn trace_state() -> trace_state_t;

        pub fn trace_is_category_enabled(category_literal: *const libc::c_char) -> bool;

        pub fn trace_acquire_context() -> *const trace_context_t;

        pub fn trace_acquire_context_for_category(
            category_literal: *const libc::c_char,
            out_ref: *mut trace_string_ref_t,
        ) -> *const trace_context_t;

        pub fn trace_release_context(context: *const trace_context_t);

        pub fn trace_register_observer(event: zx_handle_t) -> zx_status_t;

        pub fn trace_unregister_observer(event: zx_handle_t) -> zx_status_t;

        pub fn trace_notify_observer_updated(event: zx_handle_t);
    }
}

#[cfg(test)]
mod test {
    use crate::{trim_to_last_char_boundary, Scope};

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
    fn alert() {
        alert!("foo", "bar");
    }

    #[test]
    fn counter() {
        let id = 24601;
        counter!("foo", "bar", id, "x" => 5, "y" => 10);
    }

    #[test]
    fn duration() {
        duration!("foo", "bar", "x" => 5, "y" => 10);
        println!("Between duration creation and duration ending");
    }

    #[test]
    fn duration_begin_end() {
        duration_begin!("foo", "bar", "x" => 5);
        println!("Between duration creation and duration ending");
        duration_end!("foo", "bar", "y" => 10);
    }

    #[test]
    fn trace_enabled() {
        if crate::is_enabled() {
            println!("Tracing enabled");
        }
    }
}
