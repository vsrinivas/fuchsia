#![feature(test)]
extern crate c2_chacha;
extern crate stream_cipher;
extern crate test;

use c2_chacha::simd::machine::x86;
use c2_chacha::simd::Machine;
use c2_chacha::ChaChaState;
use test::Bencher;

macro_rules! mach_bench {
    ($MachName:ident, $feature:expr, $enable:expr) => {
        #[allow(non_snake_case)]
        #[bench]
        pub fn $MachName(b: &mut Bencher) {
            if !$enable {
                return;
            }
            let m = unsafe { x86::$MachName::instance() };
            let z = m
                .vec::<<x86::$MachName as Machine>::u64x2, _>([0x0, 0x0])
                .into();
            let mut state = ChaChaState { b: z, c: z, d: z };
            let mut out = [0; 256];
            #[target_feature(enable = $feature)]
            unsafe fn runner<M: Machine>(m: M, state: &mut ChaChaState, out: &mut [u8; 256]) {
                c2_chacha::refill_wide_impl(m, state, 40 * 20 / 2, out)
            }
            b.iter(|| unsafe { runner(m, &mut state, &mut out) });
            b.bytes = 10240;
        }
    };
}

mach_bench!(SSE2, "sse2", is_x86_feature_detected!("sse2"));
mach_bench!(SSSE3, "ssse3", is_x86_feature_detected!("ssse3"));
mach_bench!(SSE41, "sse4.1", is_x86_feature_detected!("sse4.1"));
mach_bench!(AVX, "avx", is_x86_feature_detected!("avx"));
mach_bench!(AVX2, "avx2", is_x86_feature_detected!("avx2"));
