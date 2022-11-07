//! Types supporting the UTF-8 parser
#![allow(non_camel_case_types)]
use core::mem;

/// States the parser can be in.
///
/// There is a state for each initial input of the 3 and 4 byte sequences since
/// the following bytes are subject to different conditions than a tail byte.
#[allow(dead_code)]
#[derive(Debug, Copy, Clone)]
pub enum State {
    /// Ground state; expect anything
    Ground = 0,
    /// 3 tail bytes
    Tail3 = 1,
    /// 2 tail bytes
    Tail2 = 2,
    /// 1 tail byte
    Tail1 = 3,
    /// UTF8-3 starting with E0
    U3_2_e0 = 4,
    /// UTF8-3 starting with ED
    U3_2_ed = 5,
    /// UTF8-4 starting with F0
    Utf8_4_3_f0 = 6,
    /// UTF8-4 starting with F4
    Utf8_4_3_f4 = 7,
}

/// Action to take when receiving a byte
#[allow(dead_code)]
#[derive(Debug, Copy, Clone)]
pub enum Action {
    /// Unexpected byte; sequence is invalid
    InvalidSequence = 0,
    /// Received valid 7-bit ASCII byte which can be directly emitted.
    EmitByte = 1,
    /// Set the bottom continuation byte
    SetByte1 = 2,
    /// Set the 2nd-from-last continuation byte
    SetByte2 = 3,
    /// Set the 2nd-from-last byte which is part of a two byte sequence
    SetByte2Top = 4,
    /// Set the 3rd-from-last continuation byte
    SetByte3 = 5,
    /// Set the 3rd-from-last byte which is part of a three byte sequence
    SetByte3Top = 6,
    /// Set the top byte of a four byte sequence.
    SetByte4 = 7,
}

/// Convert a state and action to a u8
///
/// State will be the bottom 4 bits and action the top 4
#[inline]
#[allow(dead_code)]
pub fn pack(state: State, action: Action) -> u8 {
    ((action as u8) << 4) | (state as u8)
}

/// Convert a u8 to a state and action
///
/// # Unsafety
///
/// If this function is called with a byte that wasn't encoded with the `pack`
/// function in this module, there is no guarantee that a valid state and action
/// can be produced.
#[inline]
pub unsafe fn unpack(val: u8) -> (State, Action) {
    (
        // State is stored in bottom 4 bits
        mem::transmute(val & 0x0f),

        // Action is stored in top 4 bits
        mem::transmute(val >> 4),
    )
}
