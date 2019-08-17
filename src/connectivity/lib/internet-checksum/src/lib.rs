// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! RFC 1071 "internet checksum" computation.
//!
//! This crate implements the "internet checksum" defined in [RFC 1071] and
//! updated in [RFC 1141] and [RFC 1624], which is used by many different
//! protocols' packet formats. The checksum operates by computing the 1s
//! complement of the 1s complement sum of successive 16-bit words of the input.
//!
//! SIMD acceleration is used on some platforms (currently, x86_64 with the avx2
//! extensions).
//!
//! # Benchmarks
//!
//! The following microbenchmarks were performed on a 2018 Google Pixelbook.
//! Each benchmark constructs a [`Checksum`] object, calls
//! [`Checksum::add_bytes`] with an input of the given number of bytes, and then
//! calls [`Checksum::checksum`] to finalize. Benchmarks were performed with
//! SIMD both enabled and disabled. Average values were calculated over 3
//! trials.
//!
//! Bytes | Time w/o SIMD | Rate w/o SIMD | Time w/ SIMD | Rate w/ SIMD | Ratio (time w / time w/o)
//! ----- | ------------- | ------------- | ------------ | ------------ | -------------------------
//!    31 |      3,657 ns |     8.48 MB/s |     3,692 ns |    8.40 MB/s |                      1.01
//!    32 |      3,735 ns |     8.57 MB/s |     3,767 ns |    8.50 MB/s |                      1.01
//!    64 |      7,092 ns |     9.02 MB/s |     6,580 ns |    9.73 MB/s |                      0.93
//!   128 |     13,790 ns |     9.28 MB/s |     7,428 ns |    17.2 MB/s |                      0.54
//!   256 |     27,169 ns |     9.42 MB/s |     9,224 ns |    27.8 MB/s |                      0.34
//!  1024 |    107,609 ns |     9.52 MB/s |    20,071 ns |    51.0 MB/s |                      0.19
//!
//! [RFC 1071]: https://tools.ietf.org/html/rfc1071
//! [RFC 1141]: https://tools.ietf.org/html/rfc1141
//! [RFC 1624]: https://tools.ietf.org/html/rfc1624

// Optimizations applied:
//
// 0. Byteorder independence: as described in RFC 1071 section 2.(B)
//    The sum of 16-bit integers can be computed in either byte order,
//    so this actually saves us from the unnecessary byte swapping on
//    an LE machine. As perfed on a gLinux workstation, that swapping
//    can account for ~20% of the runtime.
//
// 1. Widen the accumulator: doing so enables us to process a bigger
//    chunk of data once at a time, achieving some kind of poor man's
//    SIMD. Currently a u128 counter is used on x86-64 and a u64 is
//    used conservatively on other architectures.
//
// 2. Process more at a time: the old implementation uses a u32 accumulator
//    but it only adds one u16 each time to implement deferred carry. In
//    the current implementation we are processing a u128 once at a time
//    on x86-64, which is 8 u16's. On other platforms, we are processing
//    a u64 at a time, which is 4 u16's.
//
// 3. Induce the compiler to produce `adc` instruction: this is a very
//    useful instruction to implement 1's complement addition and available
//    on both x86 and ARM. The functions `adc_uXX` are for this use.
//
// 4. Eliminate branching as much as possible: the old implementation has
//    if statements for detecting overflow of the u32 accumulator which
//    is not needed when we can access the carry flag with `adc`. The old
//    `normalize` function used to have a while loop to fold the u32,
//    however, we can unroll that loop because we know ahead of time how
//    much additions we need.
//
// 5. In the loop of `add_bytes`, the `adc_u64` is not used, instead,
//    the `overflowing_add` is directly used. `adc_u64`'s carry flag
//    comes from the current number being added while the slightly
//    convoluted version in `add_bytes`, adding each number depends on
//    the carry flag of the previous computation. I checked under release
//    mode this issues 3 instructions instead of 4 for x86 and it should
//    theoretically be beneficial, however, measurement showed me that it
//    helps only a little. So this trick is not used for `update`.
//
// 6. When the input is small, fallback to deferred carry method. Deferred
//    carry turns out to be very efficient when dealing with small buffers:
//    If the input is small, the cost to deal with the tail may already
//    outweigh the benefit of the unrolling itself. Some measurement
//    confirms this theory.
//
// Results:
//
// Micro-benchmarks are run on an x86-64 gLinux workstation. In summary,
// compared the baseline 0 which is prior to the byteorder independence
// patch, there is a ~4x speedup and the current non-simd version is faster
// than the simd version of that baseline version.
//
// TODO: run this optimization on other platforms. I would expect
// the situation on ARM a bit different because I am not sure
// how much penalty there will be for misaligned read on ARM, or
// whether it is even supported (On x86 there is generally no
// penalty for misaligned read). If there will be penalties, we
// should consider alignment as an optimization opportunity on ARM.

// TODO(joshlf): Right-justify the columns above

#![cfg_attr(feature = "benchmark", feature(test))]

#[cfg(all(test, feature = "benchmark"))]
extern crate test;

#[cfg(target_arch = "x86_64")]
use core::arch::x86_64;

use byteorder::{ByteOrder, NativeEndian};

// TODO(joshlf):
// - Investigate optimizations proposed in RFC 1071 Section 2. The most
//   promising on modern hardware is probably (C) Parallel Summation, although
//   that needs to be balanced against (1) Deferred Carries. Benchmarks will
//   need to be performed to determine which is faster in practice, and under
//   what scenarios.

/// Compute the checksum of "bytes".
///
/// `checksum(bytes)` is shorthand for:
///
/// ```rust
/// # use internet_checksum::Checksum;
/// # let bytes = &[];
/// # let _ = {
/// let mut c = Checksum::new();
/// c.add_bytes(bytes);
/// c.checksum()
/// # };
/// ```
#[inline]
pub fn checksum(bytes: &[u8]) -> [u8; 2] {
    let mut c = Checksum::new();
    c.add_bytes(bytes);
    c.checksum()
}

#[cfg(target_arch = "x86_64")]
type Accumulator = u128;
#[cfg(not(target_arch = "x86_64"))]
type Accumulator = u64;

/// The threshold for small buffers, if the buffer is too small,
/// fall back to the normal deferred carry method where a wide
/// accumulator is used but one `u16` is added once at a time.
// TODO: `64` works fine on x86_64, but this value may be different
// on other platforms.
const SMALL_BUF_THRESHOLD: usize = 64;

/// The following macro unrolls operations on u16's to wider integers.
///
/// # Arguments
///
/// * `$arr`  - The byte slice being processed.
/// * `$body` - The operation to operate on the wider integer. It should
///             be a macro because functions are not options here.
///
///
/// This macro will choose the "wide integer" for you, on x86-64,
/// it will choose u128 as the "wide integer" and u64 anywhere else.
macro_rules! loop_unroll {
    (@inner $arr: ident, 16, $body:ident) => {
        while $arr.len() >= 16 {
            $body!(16, read_u128);
        }
        unroll_tail!($arr, 16, $body);
    };

    (@inner $arr: ident, 8, $body:ident) => {
        while $arr.len() >= 8 {
            $body!(8, read_u64);
        }
        unroll_tail!($arr, 8, $body);
    };

    ($arr: ident, $body: ident) => {
        #[cfg(target_arch = "x86_64")]
        loop_unroll!(@inner $arr, 16, $body);
        #[cfg(not(target_arch = "x86_64"))]
        loop_unroll!(@inner $arr, 8, $body);
    };
}

/// At the the end of loop unrolling, we have to take care of bytes
/// that are left over. For example, `unroll_tail!(bytes, 4, body)`
/// expands to
/// ```
/// if bytes.len & 2 != 0 {
///   body!(2, read_u16);
/// }
/// ```
macro_rules! unroll_tail {
    ($arr: ident, $n: literal, $read: ident, $body: ident) => {
        if $arr.len() & $n != 0 {
            $body!($n, $read);
        }
    };

    ($arr: ident, 4, $body: ident) => {
        unroll_tail!($arr, 2, read_u16, $body);
    };

    ($arr: ident, 8, $body: ident) => {
        unroll_tail!($arr, 4, read_u32, $body);
        unroll_tail!($arr, 4, $body);
    };

    ($arr: ident, 16, $body: ident) => {
        unroll_tail!($arr, 8, read_u64, $body);
        unroll_tail!($arr, 8, $body);
    };
}

/// Updates bytes in an existing checksum.
///
/// `update` updates a checksum to reflect that the already-checksummed bytes
/// `old` have been updated to contain the values in `new`. It implements the
/// algorithm described in Equation 3 in [RFC 1624]. The first byte must be at
/// an even number offset in the original input. If an odd number offset byte
/// needs to be updated, the caller should simply include the preceding byte as
/// well. If an odd number of bytes is given, it is assumed that these are the
/// last bytes of the input. If an odd number of bytes in the middle of the
/// input needs to be updated, the preceding or following byte of the input
/// should be added to make an even number of bytes.
///
/// # Panics
///
/// `update` panics if `old.len() != new.len()`.
///
/// [RFC 1624]: https://tools.ietf.org/html/rfc1624
#[inline]
pub fn update(checksum: [u8; 2], old: &[u8], new: &[u8]) -> [u8; 2] {
    assert_eq!(old.len(), new.len());
    // We compute on the sum, not the one's complement of the sum. checksum
    // is the one's complement of the sum, so we need to get back to the
    // sum. Thus, we negate checksum.
    // HC' = ~HC
    let mut sum = !NativeEndian::read_u16(&checksum[..]) as Accumulator;

    // Let's reuse `Checksum::add_bytes` to update our checksum
    // so that we can get the speedup for free. Using
    // [RFC 1071 Eqn. 3], we can efficiently update our new checksum.
    let mut c1 = Checksum::new();
    let mut c2 = Checksum::new();
    c1.add_bytes(old);
    c2.add_bytes(new);

    // Note, `c1.checksum_inner()` is actually ~m in [Eqn. 3]
    // `c2.checksum_inner()` is actually ~m' in [Eqn. 3]
    // so we have to negate `c2.checksum_inner()` first to get m'.
    // HC' += ~m, c1.checksum_inner() == ~m.
    sum = adc_accumulator(sum, c1.checksum_inner() as Accumulator);
    // HC' += m', c2.checksum_inner() == ~m'.
    sum = adc_accumulator(sum, !c2.checksum_inner() as Accumulator);
    // HC' = ~HC.
    let mut cksum = [0u8; 2];
    NativeEndian::write_u16(&mut cksum[..], !normalize(sum));
    cksum
}

/// RFC 1071 "internet checksum" computation.
///
/// `Checksum` implements the "internet checksum" defined in [RFC 1071] and
/// updated in [RFC 1141] and [RFC 1624], which is used by many different
/// protocols' packet formats. The checksum operates by computing the 1s
/// complement of the 1s complement sum of successive 16-bit words of the input.
///
/// [RFC 1071]: https://tools.ietf.org/html/rfc1071
/// [RFC 1141]: https://tools.ietf.org/html/rfc1141
/// [RFC 1624]: https://tools.ietf.org/html/rfc1624
#[derive(Default)]
pub struct Checksum {
    sum: Accumulator,
    // Since odd-length inputs are treated specially, we store the trailing byte
    // for use in future calls to add_bytes(), and only treat it as a true
    // trailing byte in checksum().
    trailing_byte: Option<u8>,
}

impl Checksum {
    /// Minimum number of bytes in a buffer to run the SIMD algorithm.
    ///
    /// Running the algorithm with less than `MIN_BYTES_FOR_SIMD` bytes will
    /// cause the benefits of SIMD to be dwarfed by the overhead (performing
    /// worse than the normal/non-SIMD algorithm). This value was chosen after
    /// several benchmarks which showed that the algorithm performed worse than
    /// the normal/non-SIMD algorithm when the number of bytes was less than 256.
    // TODO: 256 may not perform the best on other platforms such as ARM.
    #[cfg(target_arch = "x86_64")]
    const MIN_BYTES_FOR_SIMD: usize = 256;

    /// Initialize a new checksum.
    #[inline]
    pub const fn new() -> Self {
        Checksum { sum: 0, trailing_byte: None }
    }

    /// Add bytes to the checksum.
    ///
    /// If `bytes` does not contain an even number of bytes, a single zero byte
    /// will be added to the end before updating the checksum.
    ///
    /// Note that `add_bytes` has some fixed overhead regardless of the size of
    /// `bytes`. Additionally, SIMD optimizations are only available for inputs
    /// of a certain size. Where performance is a concern, prefer fewer calls to
    /// `add_bytes` with larger input over more calls with smaller input.
    #[inline]
    pub fn add_bytes(&mut self, mut bytes: &[u8]) {
        if bytes.len() < SMALL_BUF_THRESHOLD {
            self.add_bytes_small(bytes);
            return;
        }

        let mut sum = self.sum;
        let mut carry = false;

        // We are not using `adc_uXX` functions here, instead,
        // we manually track the carry flag. This is because
        // in `adc_uXX` functions, the carry flag depends on
        // addition itself. So the assembly for that function
        // reads as follows:
        //
        // mov %rdi, %rcx
        // mov %rsi, %rax
        // add %rcx, %rsi -- waste! only used to generate CF.
        // adc %rdi, $rax -- the real useful instruction.
        //
        // So we had better to make us depend on the CF generated
        // by the addition of the previous 16-bit word. The ideal
        // assembly should look like:
        //
        // add 0(%rdi), %rax
        // adc 8(%rdi), %rax
        // adc 16(%rdi), %rax
        // .... and so on ...
        //
        // Sadly, there are too many instructions that can affect
        // the carry flag, and LLVM is not that optimized to find
        // out the pattern and let all these adc instructions not
        // interleaved. However, doing so results in 3 instructions
        // instead of the original 4 instructions (the two mov's are
        // still there) and it makes a difference on input size like
        // 1023.

        // The following macro is used as a `body` when invoking a
        // `loop_unroll` macro. `$step` means how many bytes to handle
        // at once; `$read` is supposed to be `read_u16`, `read_u32`
        // and so on, it is used to get an unsigned integer of `$step`
        // width from a byte slice; `$bytes` is the byte slice mentioned
        // before, if omitted, it defaults to be `bytes`, which is the
        // argument of the surrounding function.
        macro_rules! update_sum_carry {
            ($step: literal, $read: ident, $bytes: expr) => {
                let (s, c) = sum.overflowing_add(NativeEndian::$read($bytes) as Accumulator);
                sum = s + (carry as Accumulator);
                carry = c;
                bytes = &bytes[$step..];
            };
            ($step: literal, $read: ident) => {
                update_sum_carry!($step, $read, bytes);
            };
        }

        // if there's a trailing byte, consume it first
        if let Some(byte) = self.trailing_byte {
            update_sum_carry!(1, read_u16, &[byte, bytes[0]]);
            self.trailing_byte = None;
        }

        // First, process as much as we can with SIMD.
        bytes = Self::add_bytes_simd(&mut sum, bytes);

        loop_unroll!(bytes, update_sum_carry);

        if bytes.len() == 1 {
            self.trailing_byte = Some(bytes[0]);
        }

        self.sum = sum + (carry as Accumulator);
    }

    /// The efficient fallback when the buffer is small.
    ///
    /// In this implementation, one `u16` is added once a
    /// time, so we don't waste time on dealing with the
    /// tail of the buffer. Besides, given that the accumulator
    /// is large enough, when inputs are small, there should
    /// hardly be overflows, so for any modern architecture,
    /// there is little chance in misprediction.
    // The inline attribute is needed here, micro benchmarks showed
    // that it speeds up things.
    #[inline(always)]
    fn add_bytes_small(&mut self, mut bytes: &[u8]) {
        if bytes.is_empty() {
            return;
        }

        let mut sum = self.sum;
        fn update_sum(acc: Accumulator, rhs: u16) -> Accumulator {
            if let Some(updated) = acc.checked_add(rhs as Accumulator) {
                updated
            } else {
                (normalize(acc) + rhs) as Accumulator
            }
        }

        if let Some(byte) = self.trailing_byte {
            sum = update_sum(sum, NativeEndian::read_u16(&[byte, bytes[0]]));
            bytes = &bytes[1..];
            self.trailing_byte = None;
        }

        while bytes.len() >= 2 {
            sum = update_sum(sum, NativeEndian::read_u16(bytes));
            bytes = &bytes[2..];
        }

        if bytes.len() == 1 {
            self.trailing_byte = Some(bytes[0]);
        }

        self.sum = sum;
    }

    /// Computes the checksum, but in big endian byte order.
    fn checksum_inner(&self) -> u16 {
        let mut sum = self.sum;
        if let Some(byte) = self.trailing_byte {
            sum = adc_accumulator(sum, NativeEndian::read_u16(&[byte, 0]) as Accumulator);
        }
        !normalize(sum)
    }

    /// Computes the checksum, and returns the array representation.
    ///
    /// `checksum` returns the checksum of all data added using `add_bytes` so
    /// far. Calling `checksum` does *not* reset the checksum. More bytes may be
    /// added after calling `checksum`, and they will be added to the checksum
    /// as expected.
    ///
    /// If an odd number of bytes have been added so far, the checksum will be
    /// computed as though a single 0 byte had been added at the end in order to
    /// even out the length of the input.
    #[inline]
    pub fn checksum(&self) -> [u8; 2] {
        let mut cksum = [0u8; 2];
        NativeEndian::write_u16(&mut cksum[..], self.checksum_inner());
        cksum
    }

    /// Adds bytes to a running sum using architecture specific SIMD
    /// instructions.
    ///
    /// `add_bytes_simd` updates `sum` with the sum of `bytes` using
    /// architecture-specific SIMD instructions. It may not process all bytes,
    /// and whatever bytes are not processed will be returned. If no
    /// implementation exists for the target architecture and run-time CPU
    /// features, `add_bytes_simd` does nothing and simply returns `bytes`
    /// directly.
    #[inline(always)]
    fn add_bytes_simd<'a>(sum: &mut Accumulator, bytes: &'a [u8]) -> &'a [u8] {
        #[cfg(target_arch = "x86_64")]
        {
            if is_x86_feature_detected!("avx2") && bytes.len() >= Self::MIN_BYTES_FOR_SIMD {
                return unsafe { Self::add_bytes_x86_64(sum, bytes) };
            }
        }

        // Suppress unused variable warning when we don't compile the preceding
        // block.
        #[cfg(not(target_arch = "x86_64"))]
        let _ = sum;

        bytes
    }

    /// Adds bytes to a running sum using x86_64's avx2 SIMD instructions.
    ///
    /// # Safety
    ///
    /// `add_bytes_x86_64` should never be called unless the run-time CPU
    /// features include 'avx2'. If `add_bytes_x86_64` is called and the
    /// run-time CPU features do not include 'avx2', it is considered undefined
    /// behaviour.
    #[cfg(target_arch = "x86_64")]
    #[target_feature(enable = "avx2")]
    unsafe fn add_bytes_x86_64<'a>(sum: &mut Accumulator, mut bytes: &'a [u8]) -> &'a [u8] {
        // TODO(ghanan): Use safer alternatives to achieve SIMD algorithm once
        // they stabilize.

        let zeros: x86_64::__m256i = x86_64::_mm256_setzero_si256();
        let mut c0: x86_64::__m256i;
        let mut c1: x86_64::__m256i;
        let mut acc: x86_64::__m256i;
        let data: [u8; 32] = [0; 32];

        while bytes.len() >= 32 {
            let mut add_count: u32 = 0;

            // Reset accumulator.
            acc = zeros;

            // We can do (2^16 + 1) additions to the accumulator (`acc`) that
            // starts off at 0 without worrying about overflow. Since each
            // iteration of this loop does 2 additions to the accumulator,
            // `add_count` must be less than or equal to U16_MAX(= 2 ^ 16 - 1 =
            // 2 ^ 16 - 1 - 2) to guarantee no overflows during this loop
            // iteration. We know we can do 2^16 + 1 additions to the
            // accumulator because we are using 32bit integers which can hold a
            // max value of U32_MAX (2^32 - 1), and we are adding 16bit values
            // with a max value of U16_MAX (2^16 - 1). U32_MAX = (U16_MAX << 16
            // + U16_MAX) = U16_MAX * (2 ^ 16 + 1)
            while bytes.len() >= 32 && add_count <= u32::from(std::u16::MAX) {
                // Load 32 bytes from memory (16 16bit values to add to `sum`)
                //
                // `_mm256_lddqu_si256` does not require the memory address to
                // be aligned so remove the linter check for casting from a less
                // strictly-aligned pointer to a more strictly-aligned pointer.
                // https://doc.rust-lang.org/core/arch/x86_64/fn._mm256_lddqu_si256.html
                #[allow(clippy::cast_ptr_alignment)]
                {
                    c0 = x86_64::_mm256_lddqu_si256(bytes.as_ptr() as *const x86_64::__m256i);
                }

                // Create 32bit words with most significant 16 bits = 0, least
                // significant 16 bits set to a new 16 bit word to add to
                // checksum from bytes. Setting the most significant 16 bits to
                // 0 allows us to do 2^16 simd additions (2^20 16bit word
                // additions) without worrying about overflows.
                c1 = x86_64::_mm256_unpackhi_epi16(c0, zeros);
                c0 = x86_64::_mm256_unpacklo_epi16(c0, zeros);

                // Sum 'em up!
                // `acc` being treated as a vector of 8x 32bit words.
                acc = x86_64::_mm256_add_epi32(acc, c1);
                acc = x86_64::_mm256_add_epi32(acc, c0);

                // We did 2 additions to the accumulator in this iteration of
                // the loop.
                add_count += 2;

                bytes = &bytes[32..];
            }

            // Store the results of our accumlator of 8x 32bit words to our
            // temporary buffer `data` so that we can iterate over data 16 bits
            // at a time and add the values to `sum`. Since `acc` is a 256bit
            // value, it requires 32 bytes, provided by `data`.
            //
            // `_mm256_storeu_si256` does not require the memory address to be
            // aligned on any particular boundary so remove the linter check for
            // casting from a less strictly-aligned pointer to a more strictly-
            // aligned pointer.
            // https://doc.rust-lang.org/core/arch/x86_64/fn._mm256_storeu_si256.html
            #[allow(clippy::cast_ptr_alignment)]
            x86_64::_mm256_storeu_si256(data.as_ptr() as *mut x86_64::__m256i, acc);

            let mut fold = *sum;
            // Iterate over the accumulator data accumulator-width bytes at a time,
            // and add it to `sum`.
            macro_rules! fold {
                ($step: literal, $read: ident) => {
                    for x in (0..32).step_by($step) {
                        fold = adc_accumulator(
                            fold,
                            NativeEndian::$read(&data[x..x + $step]) as Accumulator,
                        );
                    }
                };
            }
            #[cfg(not(target_arch = "x86_64"))]
            fold!(8, read_u64);
            #[cfg(target_arch = "x86_64")]
            fold!(16, read_u128);
            *sum = fold;
        }

        bytes
    }
}

macro_rules! impl_adc {
    ($name: ident, $t: ty) => {
        /// implements 1's complement addition for $t,
        /// exploiting the carry flag on a 2's complement machine.
        /// In practice, the adc instruction will be generated.
        fn $name(a: $t, b: $t) -> $t {
            let (s, c) = a.overflowing_add(b);
            s + (c as $t)
        }
    };
}

impl_adc!(adc_u16, u16);
impl_adc!(adc_u32, u32);
#[cfg(target_arch = "x86_64")]
impl_adc!(adc_u64, u64);
impl_adc!(adc_accumulator, Accumulator);

/// Normalizes the accumulator by mopping up the
/// overflow until it fits in a `u16`.
fn normalize(a: Accumulator) -> u16 {
    #[cfg(target_arch = "x86_64")]
    return normalize_64(adc_u64(a as u64, (a >> 64) as u64));
    #[cfg(not(target_arch = "x86_64"))]
    return normalize_64(a);
}

fn normalize_64(a: u64) -> u16 {
    let t = adc_u32(a as u32, (a >> 32) as u32);
    adc_u16(t as u16, (t >> 16) as u16)
}

#[cfg(all(test, feature = "benchmark"))]
mod benchmarks {
    // Benchmark results for comparing checksum calculation with and without
    // SIMD implementation, running on Google's Pixelbook. Average values were
    // calculated over 3 trials.
    //
    // Number of | Average time  | Average time | Ratio
    //   bytes   | (ns) w/o SIMD | (ns) w/ SIMD | (w / w/o)
    // --------------------------------------------------
    //        31 |          3657 |         3692 |   1.01
    //        32 |          3735 |         3767 |   1.01
    //        64 |          7092 |         6580 |   0.93
    //       128 |         13790 |         7428 |   0.54
    //       256 |         27169 |         9224 |   0.34
    //      1024 |        107609 |        20071 |   0.19

    extern crate test;
    use super::*;

    /// Benchmark time to calculate checksum with a single call to `add_bytes`
    /// with 31 bytes.
    #[bench]
    fn bench_checksum_31(b: &mut test::Bencher) {
        b.iter(|| {
            let buf = test::black_box([0xFF; 31]);
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            test::black_box(c.checksum());
        });
    }

    /// Benchmark time to calculate checksum with a single call to `add_bytes`
    /// with 32 bytes.
    #[bench]
    fn bench_checksum_32(b: &mut test::Bencher) {
        b.iter(|| {
            let buf = test::black_box([0xFF; 32]);
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            test::black_box(c.checksum());
        });
    }

    /// Benchmark time to calculate checksum with a single call to `add_bytes`
    /// with 64 bytes.
    #[bench]
    fn bench_checksum_64(b: &mut test::Bencher) {
        b.iter(|| {
            let buf = test::black_box([0xFF; 64]);
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            test::black_box(c.checksum());
        });
    }

    /// Benchmark time to calculate checksum with a single call to `add_bytes`
    /// with 128 bytes.
    #[bench]
    fn bench_checksum_128(b: &mut test::Bencher) {
        b.iter(|| {
            let buf = test::black_box([0xFF; 128]);
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            test::black_box(c.checksum());
        });
    }

    /// Benchmark time to calculate checksum with a single call to `add_bytes`
    /// with 256 bytes.
    #[bench]
    fn bench_checksum_256(b: &mut test::Bencher) {
        b.iter(|| {
            let buf = test::black_box([0xFF; 256]);
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            test::black_box(c.checksum());
        });
    }

    /// Benchmark time to calculate checksum with a single call to `add_bytes`
    /// with 1024 bytes.
    #[bench]
    fn bench_checksum_1024(b: &mut test::Bencher) {
        b.iter(|| {
            let buf = test::black_box([0xFF; 1024]);
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            test::black_box(c.checksum());
        });
    }

    /// Benchmark time to calculate checksum with a single call to `add_bytes`
    /// with 1023 bytes.
    #[bench]
    fn bench_checksum_1023(b: &mut test::Bencher) {
        b.iter(|| {
            let buf = test::black_box([0xFF; 1023]);
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            test::black_box(c.checksum());
        });
    }

    #[bench]
    fn bench_checksum_20(b: &mut test::Bencher) {
        b.iter(|| {
            let buf = test::black_box([0xFF; 20]);
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            test::black_box(c.checksum());
        });
    }

    #[bench]
    fn bench_checksum_small_20(b: &mut test::Bencher) {
        b.iter(|| {
            let buf = test::black_box([0xFF; 20]);
            let mut c = Checksum::new();
            c.add_bytes_small(&buf);
            test::black_box(c.checksum());
        });
    }

    #[bench]
    fn bench_checksum_small_31(b: &mut test::Bencher) {
        b.iter(|| {
            let buf = test::black_box([0xFF; 31]);
            let mut c = Checksum::new();
            c.add_bytes_small(&buf);
            test::black_box(c.checksum());
        });
    }

    #[bench]
    fn bench_update_2(b: &mut test::Bencher) {
        b.iter(|| {
            let old = test::black_box([0x42; 2]);
            let new = test::black_box([0xa0; 2]);
            test::black_box(update([42; 2], &old[..], &new[..]));
        });
    }

    #[bench]
    fn bench_update_4(b: &mut test::Bencher) {
        b.iter(|| {
            let old = test::black_box([0x42; 4]);
            let new = test::black_box([0xa0; 4]);
            test::black_box(update([42; 2], &old[..], &new[..]));
        });
    }

    #[bench]
    fn bench_update_8(b: &mut test::Bencher) {
        b.iter(|| {
            let old = test::black_box([0x42; 8]);
            let new = test::black_box([0xa0; 8]);
            test::black_box(update([42; 2], &old[..], &new[..]));
        });
    }
}

#[cfg(test)]
mod tests {
    use core::iter;

    use byteorder::NativeEndian;
    use rand::{Rng, SeedableRng};

    use rand_xorshift::XorShiftRng;

    use super::*;

    /// Create a new deterministic RNG from a seed.
    fn new_rng(mut seed: u64) -> XorShiftRng {
        if seed == 0 {
            // XorShiftRng can't take 0 seeds
            seed = 1;
        }
        let mut bytes = [0; 16];
        NativeEndian::write_u32(&mut bytes[0..4], seed as u32);
        NativeEndian::write_u32(&mut bytes[4..8], (seed >> 32) as u32);
        NativeEndian::write_u32(&mut bytes[8..12], seed as u32);
        NativeEndian::write_u32(&mut bytes[12..16], (seed >> 32) as u32);
        XorShiftRng::from_seed(bytes)
    }

    #[test]
    fn test_checksum() {
        for buf in IPV4_HEADERS {
            // compute the checksum as normal
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            assert_eq!(c.checksum(), [0u8; 2]);
            // compute the checksum one byte at a time to make sure our
            // trailing_byte logic works
            let mut c = Checksum::new();
            for byte in *buf {
                c.add_bytes(&[*byte]);
            }
            assert_eq!(c.checksum(), [0u8; 2]);

            // Make sure that it works even if we overflow u32. Performing this
            // loop 2 * 2^16 times is guaranteed to cause such an overflow
            // because 0xFFFF + 0xFFFF > 2^16, and we're effectively adding
            // (0xFFFF + 0xFFFF) 2^16 times. We verify the overflow as well by
            // making sure that, at least once, the sum gets smaller from one
            // loop iteration to the next.
            let mut c = Checksum::new();
            c.add_bytes(&[0xFF, 0xFF]);
            for _ in 0..((2 * (1 << 16)) - 1) {
                c.add_bytes(&[0xFF, 0xFF]);
            }
            assert_eq!(c.checksum(), [0u8; 2]);
        }

        // Make sure that checksum works with add_bytes taking a buffer big
        // enough to test implementation with simd instructions and cause
        // overflow within the implementation.
        let mut c = Checksum::new();
        // 2 bytes/word * 8 words/additions * 2^16 additions/overflow
        // * 1 overflow + 79 extra bytes = 2^20 + 79
        let buf = vec![0xFF; (1 << 20) + 79];
        c.add_bytes(&buf);
        assert_eq!(c.checksum(), [0, 0xFF]);
    }

    #[test]
    fn test_checksum_simd_rand() {
        let mut rng = new_rng(70812476915813);

        // Test simd implementation with random values and buffer big enough to
        // cause an overflow within the implementation..
        // 2 bytes/word * 8 words/additions * 2^16 additions/overflow
        // * 1 overflow + 79 extra bytes
        // = 2^20 + 79
        const BUF_LEN: usize = (1 << 20) + 79;
        let buf: Vec<u8> = iter::repeat_with(|| rng.gen()).take(BUF_LEN).collect();

        let single_bytes = {
            // Add 1 byte at a time to make sure we do not enter implementation
            // with simd instructions
            let mut c = Checksum::new();
            for i in 0..BUF_LEN {
                c.add_bytes(&buf[i..=i]);
            }
            c.checksum()
        };
        let all_bytes = {
            // Calculate checksum with same buffer, but this time test the
            // implementation with simd instructions
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            c.checksum()
        };
        assert_eq!(single_bytes, all_bytes);
    }

    #[test]
    fn test_update() {
        for b in IPV4_HEADERS {
            let mut buf = Vec::new();
            buf.extend_from_slice(b);

            let mut c = Checksum::new();
            c.add_bytes(&buf);
            assert_eq!(c.checksum(), [0u8; 2]);

            // replace the destination IP with the loopback address
            let old = [buf[16], buf[17], buf[18], buf[19]];
            (&mut buf[16..20]).copy_from_slice(&[127, 0, 0, 1]);
            let updated = update(c.checksum(), &old, &[127, 0, 0, 1]);
            let from_scratch = {
                let mut c = Checksum::new();
                c.add_bytes(&buf);
                c.checksum()
            };
            assert_eq!(updated, from_scratch);
        }

        // Test update with bytes big enough to test simd implementation with
        // overflow.
        const BUF_LEN: usize = (1 << 20) + 79;
        let buf = vec![0xFF; BUF_LEN];
        let mut new_buf = buf.to_vec();
        let (begin, end) = (4, BUF_LEN);

        for i in begin..end {
            new_buf[i] = i as u8;
        }

        let updated = {
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            update(c.checksum(), &buf[begin..end], &new_buf[begin..end])
        };
        let from_scratch = {
            let mut c = Checksum::new();
            c.add_bytes(&new_buf);
            c.checksum()
        };
        assert_eq!(updated, from_scratch);
    }

    #[test]
    fn test_smoke_update() {
        let mut rng = new_rng(70_812_476_915_813);

        for _ in 0..2048 {
            // use an odd length so we test the odd length logic
            const BUF_LEN: usize = 31;
            let buf: [u8; BUF_LEN] = rng.gen();
            let mut c = Checksum::new();
            c.add_bytes(&buf);

            let (begin, end) = loop {
                let begin = rng.gen::<usize>() % BUF_LEN;
                let end = begin + (rng.gen::<usize>() % (BUF_LEN + 1 - begin));
                // update requires that begin is even and end is either even or
                // the end of the input
                if begin % 2 == 0 && (end % 2 == 0 || end == BUF_LEN) {
                    break (begin, end);
                }
            };

            let mut new_buf = buf;
            for i in begin..end {
                new_buf[i] = rng.gen();
            }
            let updated = update(c.checksum(), &buf[begin..end], &new_buf[begin..end]);
            let from_scratch = {
                let mut c = Checksum::new();
                c.add_bytes(&new_buf);
                c.checksum()
            };
            assert_eq!(updated, from_scratch);
        }
    }

    #[test]
    fn test_update_simd_rand() {
        let mut rng = new_rng(70812476915813);

        // Test updating with random values and update size big enough to test
        // simd implementation with overflow
        const MIN_BYTES: usize = 1 << 20;
        const BUF_LEN: usize = (1 << 21) + 79;
        let buf: Vec<u8> = iter::repeat_with(|| rng.gen()).take(BUF_LEN).collect();
        let orig_checksum = {
            let mut c = Checksum::new();
            c.add_bytes(&buf);
            c.checksum()
        };

        let (begin, end) = loop {
            let begin = rng.gen::<usize>() % ((BUF_LEN - MIN_BYTES) / 2);
            let end = begin
                + MIN_BYTES
                + (rng.gen::<usize>() % (((BUF_LEN - MIN_BYTES) / 2) + 1 - begin));
            // update requires that begin is even and end is either even or the
            // end of the input
            if begin % 2 == 0 && (end % 2 == 0 || end == BUF_LEN) {
                break (begin, end);
            }
        };

        let mut new_buf: Vec<u8> = buf.to_vec();

        for i in begin..end {
            new_buf[i] = rng.gen();
        }

        let from_update = update(orig_checksum, &buf[begin..end], &new_buf[begin..end]);
        let from_scratch = {
            let mut c = Checksum::new();
            c.add_bytes(&new_buf);
            c.checksum()
        };
        assert_eq!(from_scratch, from_update);
    }

    #[test]
    fn test_add_bytes_small_prop_test() {
        // Since we have two independent implementations
        // Now it is time for us to write a property test
        // to ensure the checksum algorithm(s) are indeed correct.

        let mut rng = new_rng(123478012483);
        let mut c1 = Checksum::new();
        let mut c2 = Checksum::new();
        for len in 64..1_025 {
            for _ in 0..4 {
                let mut buf = vec![];
                for _ in 0..len {
                    buf.push(rng.gen());
                }
                c1.add_bytes(&buf[..]);
                c2.add_bytes_small(&buf[..]);
                assert_eq!(c1.checksum(), c2.checksum());
                let n1 = c1.checksum_inner();
                let n2 = c2.checksum_inner();
                assert_eq!(n1, n2);
                let mut t1 = Checksum::new();
                let mut t2 = Checksum::new();
                let mut t3 = Checksum::new();
                t3.add_bytes(&buf[..]);
                if buf.len() % 2 == 1 {
                    buf.push(0);
                }
                assert_eq!(buf.len() % 2, 0);
                buf.extend_from_slice(&t3.checksum());
                t1.add_bytes(&buf[..]);
                t2.add_bytes_small(&buf[..]);
                assert_eq!(t1.checksum(), [0, 0]);
                assert_eq!(t2.checksum(), [0, 0]);
            }
        }
    }

    /// IPv4 headers.
    ///
    /// This data was obtained by capturing live network traffic.
    const IPV4_HEADERS: &[&[u8]] = &[
        &[
            0x45, 0x00, 0x00, 0x34, 0x00, 0x00, 0x40, 0x00, 0x40, 0x06, 0xae, 0xea, 0xc0, 0xa8,
            0x01, 0x0f, 0xc0, 0xb8, 0x09, 0x6a,
        ],
        &[
            0x45, 0x20, 0x00, 0x74, 0x5b, 0x6e, 0x40, 0x00, 0x37, 0x06, 0x5c, 0x1c, 0xc0, 0xb8,
            0x09, 0x6a, 0xc0, 0xa8, 0x01, 0x0f,
        ],
        &[
            0x45, 0x20, 0x02, 0x8f, 0x00, 0x00, 0x40, 0x00, 0x3b, 0x11, 0xc9, 0x3f, 0xac, 0xd9,
            0x05, 0x6e, 0xc0, 0xa8, 0x01, 0x0f,
        ],
    ];
}
