//! 64-bit counter falvors.
use super::CtrFlavor;
use cipher::generic_array::{
    typenum::{operator_aliases::PartialQuot, type_operators::PartialDiv, Unsigned, U8},
    ArrayLength, GenericArray,
};
use core::convert::TryInto;

type ChunkSize = U8;
type Chunks<B> = PartialQuot<B, ChunkSize>;
const CS: usize = ChunkSize::USIZE;

/// 64-bit big endian counter flavor.
#[derive(Default, Copy, Clone)]
#[repr(transparent)]
pub struct Ctr64BE(u64);

impl<B> CtrFlavor<B> for Ctr64BE
where
    Self: Default + Clone,
    B: ArrayLength<u8> + PartialDiv<ChunkSize>,
    Chunks<B>: ArrayLength<u64>,
{
    type Nonce = GenericArray<u64, Chunks<B>>;
    type Backend = u64;

    #[inline]
    fn generate_block(&self, nonce: &Self::Nonce) -> GenericArray<u8, B> {
        let mut block = GenericArray::<u8, B>::default();
        for i in 0..Chunks::<B>::USIZE {
            let t = if i == Chunks::<B>::USIZE - 1 {
                self.0.wrapping_add(nonce[i]).to_be_bytes()
            } else {
                nonce[i].to_ne_bytes()
            };
            block[CS * i..][..CS].copy_from_slice(&t);
        }
        block
    }

    #[inline]
    fn load(block: &GenericArray<u8, B>) -> Self::Nonce {
        let mut res = Self::Nonce::default();
        for i in 0..Chunks::<B>::USIZE {
            let chunk = block[CS * i..][..CS].try_into().unwrap();
            res[i] = if i == Chunks::<B>::USIZE - 1 {
                u64::from_be_bytes(chunk)
            } else {
                u64::from_ne_bytes(chunk)
            }
        }
        res
    }

    #[inline]
    fn checked_add(&self, rhs: usize) -> Option<Self> {
        rhs.try_into()
            .ok()
            .and_then(|rhs| self.0.checked_add(rhs))
            .map(Self)
    }

    #[inline]
    fn increment(&mut self) {
        self.0 = self.0.wrapping_add(1);
    }

    #[inline]
    fn to_backend(&self) -> Self::Backend {
        self.0
    }

    #[inline]
    fn from_backend(v: Self::Backend) -> Self {
        Self(v)
    }
}

/// 64-bit little endian counter flavor.
#[derive(Default, Clone)]
#[repr(transparent)]
pub struct Ctr64LE(u64);

impl<B> CtrFlavor<B> for Ctr64LE
where
    Self: Default + Clone,
    B: ArrayLength<u8> + PartialDiv<ChunkSize>,
    Chunks<B>: ArrayLength<u64>,
{
    type Nonce = GenericArray<u64, Chunks<B>>;
    type Backend = u64;

    #[inline]
    fn generate_block(&self, nonce: &Self::Nonce) -> GenericArray<u8, B> {
        let mut block = GenericArray::<u8, B>::default();
        for i in 0..Chunks::<B>::USIZE {
            let t = if i == 0 {
                self.0.wrapping_add(nonce[i]).to_le_bytes()
            } else {
                nonce[i].to_ne_bytes()
            };
            block[CS * i..][..CS].copy_from_slice(&t);
        }
        block
    }

    #[inline]
    fn load(block: &GenericArray<u8, B>) -> Self::Nonce {
        let mut res = Self::Nonce::default();
        for i in 0..Chunks::<B>::USIZE {
            let chunk = block[CS * i..][..CS].try_into().unwrap();
            res[i] = if i == 0 {
                u64::from_le_bytes(chunk)
            } else {
                u64::from_ne_bytes(chunk)
            }
        }
        res
    }

    #[inline]
    fn checked_add(&self, rhs: usize) -> Option<Self> {
        rhs.try_into()
            .ok()
            .and_then(|rhs| self.0.checked_add(rhs))
            .map(Self)
    }

    #[inline]
    fn increment(&mut self) {
        self.0 = self.0.wrapping_add(1);
    }

    #[inline]
    fn to_backend(&self) -> Self::Backend {
        self.0
    }

    #[inline]
    fn from_backend(v: Self::Backend) -> Self {
        Self(v)
    }
}
