// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

mod frame_writer;
mod header;
mod ie;

use proc_macro_hack::proc_macro_hack;

///! A procedural macro for writing frames into buffers.

/// This macro allows for writing frames into a buffer. The macro operates in three steps:
/// (1) Compute the frame's resulting length by adding up all headers and IEs.
/// (2) Request a buffer from a given provider with a minimum buffer length of the computed length.
/// (3) Write all headers and IEs into the given buffer.
///
/// The buffer and amount of bytes written will be returned to the caller.
///
/// # Buffer Source
/// A buffer can be provided through either a `BufferProvider` or an expression.
/// * `write_frame!` requires a `BufferProvider`:
///   ```
///   let (buf, bytes_written) = write_frame!(my_buffer_provider, { ... })?;
///   let (buf, bytes_written) = write_frame!(make_buffer_provider(), { ... })?;
///   let (buf, bytes_written) = write_frame!(self.buf_provider, { ... })?;
///   ```
///   Note: the `BufferProvider` can be any struct which provides a
///   `get_buffer(usize) -> Result<_, Error>` implementation. The returned buffer type must
///   implement the `Appendable` trait.
/// * `write_frame_with_dynamic_buf!` works with dynamically sized buffers. Such buffers can grow
///   in size and must implement the `Appendable` trait.
/// * `write_frame_with_fixed_buf!` works with fixed sized buffers. Such buffers must implement the
///   `AsMut<[u8]>` trait.
///   ```
///   let (buf, bytes_written) = write_frame_with_fixed_buf!([0u8; 20], { ... })?;
///   let (buf, bytes_written) = write_frame_with_dynamic_buf!(vec![], { ... })?;
///   ```
///
/// # Headers
/// Headers can be of arbitrary type, however, a header type must derive zerocopy's various traits
/// and declared to be packed & C-compatible.
/// Headers are declared through a qualified path to the type followed by an expression evaluating
/// to this type. For example:
/// ```
///  mac::MgmtHdr: &mac::MgmtHdr {
///      frame_ctrl: mac::FrameControl(0)
///          .with_frame_type(mac::FrameType::MGMT)
///          .with_mgmt_subtype(mac::MgmtSubtype::PROBE_REQ),
///      duration: 0,
///      addr1: mac::BCAST_ADDR,
///      addr2: self.iface_mac,
///      addr3: mac::BCAST_ADDR,
///      seq_ctrl: mac::SequenceControl(0)
///          .with_seq_num(self.ctx.seq_mgr.next_sns1(&mac::BCAST_ADDR) as u16),
///  },
/// ```
/// The value expression can be any valid Rust expression evaluating to the corresponding type
/// including but not limited to function & method calls, match- & ifstatements and constants.
///
/// # IEs
/// ## Supported IEs
/// * ssid: a byte slice
/// * supported_rates: a byte slice
/// * dsss_param_set: wlan_common::ie::DsssParamSet
/// * extended_supported_rates: a byte slice **OR** `{}`
///                   If `{}` is used the rates supplied in the `supported_rates` IE will be
///                   continued in this one if they exceed the maximum amount of allowed supported
///                   rates.
///                   Note: `extended_supported_rates` cannot be used without also declaring
///                         `supported_rates` first. `extended_supported_rates` may not directly
///                         follow the `supported_rates` IE.
/// * tim: wlan_common::ie::TimView
/// * ht_caps: wlan_common::ie::HtCapabilities
/// * vht_caps: wlan_common::ie::VhtCapabilities
/// * rsne: wlan_common::ie::rsn::rsne::Rsne
/// * bss_max_idle_period: wlan_common::ie::BssMaxIdlePeriod
///
/// ## IE values
/// IE values may be arbitrary expressions such as function calls, literals, constants or
/// variable. For example the following expressions are valid:
/// ```
///  ssid: vec![4u8; 12],
///  ssid: SSID_CONSTANT,
///  ssid: b"foobar",
///  ssid: self.generate_ssid(),
///  ssid: local_var,
///  ssid: match x { 0 => b"foo", _ => b"bar" },
///  ssid: if x { b"foo" } else { b"bar" },
/// ```
///
/// ## Optional IEs
/// IEs may be optional (see examples below). Optional IEs are declared through an `?` token.
/// Value expressions in optional IEs must evaluate to a value of type `Option<V>` with `V` being
/// a compatible value type. If the value is declared through an if-statement and **NO** else branch
/// was defined, the then-branch's value must be of type `V` and will automatically be wrapped with
/// `Some(_)` if the condition was met. If the condition was not met, `None` will be used.
/// If an else-brach was defined both branches must return an `Option<V>` value.
///
/// ## Emit IE offset
/// One can emit the offset at which a particular IE is written at.
/// ```
/// let mut offset = 0;
/// let (buf, bytes_written) = write_frame!(buf_provider, {
///     ...
///     ies: {
///         ssid: ssid,
///         supported_rates: &rates,
///         offset @ extended_supported_rates: {/* continue rates */},
///     },
///     ...
/// });
/// ```
///
/// # Payload
/// An arbitrary payload can be written to the end of the buffer. The payload's type must be a slice
/// of a compatible type.
///
/// # Return type
/// The macro returns a `Result<(Inbuf, usize), Error>`. If the frame was written successfully,
/// `Ok((buffer, bytes_written))` is returned, `Err(_)` otherwise.
///
/// # Constraints
/// The macro early returns with an error if the writing to the buffer failed, thus, the caller must
/// carry a return type of `Result<(), Error>`.
/// This macro is heavily dependent on the `wlan_common` crates and assumes the caller
/// to depend on those.
///
/// # Examples
/// ```
/// let (buf, bytes_written) = write_frame!(&mut self.ctx.buf_provider, {
///     headers: {
///         mac::MgmtHdr: &mac::MgmtHdr {
///             frame_ctrl: mac::FrameControl(0)
///                 .with_frame_type(mac::FrameType::MGMT)
///                 .with_mgmt_subtype(mac::MgmtSubtype::PROBE_REQ),
///             duration: 0,
///             addr1: mac::BCAST_ADDR,
///             addr2: self.iface_mac,
///             addr3: mac::BCAST_ADDR,
///             seq_ctrl: mac::SequenceControl(0)
///                 .with_seq_num(self.ctx.seq_mgr.next_sns1(&mac::BCAST_ADDR) as u16),
///         },
///     },
///     ies: {
///         ssid: ssid,
///         supported_rates: &rates,
///         offset @ extended_supported_rates: {/* continue rates */},
///         ht_cap?: if band_info.ht_supported {
///             band_info.ht_caps.into()
///         },
///         vht_cap?: if band_info.vht_supported {
///             band_info.vht_caps.into()
///         }
///     },
///     payload: vec![42u8; 5],
/// });
/// ```
#[proc_macro_hack]
pub fn write_frame(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    frame_writer::process_with_buf_provider(input)
}

#[proc_macro_hack]
pub fn write_frame_with_fixed_buf(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    frame_writer::process_with_fixed_buf(input)
}

#[proc_macro_hack]
pub fn write_frame_with_dynamic_buf(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    frame_writer::process_with_dynamic_buf(input)
}
