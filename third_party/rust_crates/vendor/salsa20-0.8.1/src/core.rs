//! The Salsa20 core function.

use crate::{rounds::Rounds, Key, Nonce, BLOCK_SIZE, CONSTANTS, STATE_WORDS};
use core::{convert::TryInto, marker::PhantomData, mem};

/// The Salsa20 core function.
// TODO(tarcieri): zeroize support
pub struct Core<R: Rounds> {
    /// Internal state of the core function
    state: [u32; STATE_WORDS],

    /// Number of rounds to perform
    rounds: PhantomData<R>,
}

impl<R: Rounds> Core<R> {
    /// Initialize core function with the given key and IV
    pub fn new(key: &Key, iv: &Nonce) -> Self {
        #[allow(unsafe_code)]
        let mut state: [u32; STATE_WORDS] = unsafe { mem::zeroed() };
        state[0] = CONSTANTS[0];

        for (i, chunk) in key[..16].chunks(4).enumerate() {
            state[1 + i] = u32::from_le_bytes(chunk.try_into().unwrap());
        }

        state[5] = CONSTANTS[1];

        for (i, chunk) in iv.chunks(4).enumerate() {
            state[6 + i] = u32::from_le_bytes(chunk.try_into().unwrap());
        }

        state[8] = 0;
        state[9] = 0;
        state[10] = CONSTANTS[2];

        for (i, chunk) in key[16..].chunks(4).enumerate() {
            state[11 + i] = u32::from_le_bytes(chunk.try_into().unwrap());
        }

        state[15] = CONSTANTS[3];

        Self {
            state,
            rounds: PhantomData,
        }
    }

    /// Generate output, overwriting data already in the buffer
    pub fn generate(&mut self, output: &mut [u8]) {
        debug_assert_eq!(output.len(), BLOCK_SIZE);

        let mut state = self.state;
        self.rounds(&mut state);

        for (i, chunk) in output.chunks_mut(4).enumerate() {
            chunk.copy_from_slice(&state[i].to_le_bytes());
        }
    }

    /// Apply generated keystream to the output buffer
    pub fn apply_keystream(&mut self, counter: u64, output: &mut [u8]) {
        debug_assert_eq!(output.len(), BLOCK_SIZE);
        self.counter_setup(counter);

        let mut state = self.state;
        self.rounds(&mut state);

        for (i, chunk) in output.chunks_mut(4).enumerate() {
            for (a, b) in chunk.iter_mut().zip(&state[i].to_le_bytes()) {
                *a ^= *b;
            }
        }
    }

    #[inline]
    pub(crate) fn counter_setup(&mut self, counter: u64) {
        self.state[8] = (counter & 0xffff_ffff) as u32;
        self.state[9] = ((counter >> 32) & 0xffff_ffff) as u32;
    }

    /// Run the 20 rounds (i.e. 10 double rounds) of Salsa20
    #[inline]
    fn rounds(&mut self, state: &mut [u32; STATE_WORDS]) {
        for _ in 0..(R::COUNT / 2) {
            // column rounds
            quarter_round(0, 4, 8, 12, state);
            quarter_round(5, 9, 13, 1, state);
            quarter_round(10, 14, 2, 6, state);
            quarter_round(15, 3, 7, 11, state);

            // diagonal rounds
            quarter_round(0, 1, 2, 3, state);
            quarter_round(5, 6, 7, 4, state);
            quarter_round(10, 11, 8, 9, state);
            quarter_round(15, 12, 13, 14, state);
        }

        for (s1, s0) in state.iter_mut().zip(&self.state) {
            *s1 = s1.wrapping_add(*s0);
        }
    }
}

impl<R: Rounds> From<[u32; STATE_WORDS]> for Core<R> {
    fn from(state: [u32; STATE_WORDS]) -> Core<R> {
        Self {
            state,
            rounds: PhantomData,
        }
    }
}

#[inline]
#[allow(clippy::many_single_char_names)]
pub(crate) fn quarter_round(
    a: usize,
    b: usize,
    c: usize,
    d: usize,
    state: &mut [u32; STATE_WORDS],
) {
    let mut t: u32;

    t = state[a].wrapping_add(state[d]);
    state[b] ^= t.rotate_left(7) as u32;

    t = state[b].wrapping_add(state[a]);
    state[c] ^= t.rotate_left(9) as u32;

    t = state[c].wrapping_add(state[b]);
    state[d] ^= t.rotate_left(13) as u32;

    t = state[d].wrapping_add(state[c]);
    state[a] ^= t.rotate_left(18) as u32;
}
