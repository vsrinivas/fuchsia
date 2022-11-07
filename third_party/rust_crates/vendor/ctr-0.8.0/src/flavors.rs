//! CTR mode flavors

use cipher::{
    generic_array::{ArrayLength, GenericArray},
    SeekNum,
};

mod ctr128;
mod ctr32;
mod ctr64;

pub use ctr128::*;
pub use ctr32::*;
pub use ctr64::*;

/// Trait implemented by different counter types used in the CTR mode.
pub trait CtrFlavor<B>
where
    Self: Default + Clone,
    B: ArrayLength<u8>,
{
    /// Inner representation of nonce.
    type Nonce: Clone;
    /// Backend numeric type
    type Backend: SeekNum;

    /// Generate block for given `nonce` value.
    fn generate_block(&self, nonce: &Self::Nonce) -> GenericArray<u8, B>;

    /// Load nonce value from bytes.
    fn load(block: &GenericArray<u8, B>) -> Self::Nonce;

    /// Checked addition.
    fn checked_add(&self, rhs: usize) -> Option<Self>;

    /// Wrapped increment.
    fn increment(&mut self);

    /// Convert from a backend value
    fn from_backend(v: Self::Backend) -> Self;

    /// Convert to a backend value
    fn to_backend(&self) -> Self::Backend;
}
