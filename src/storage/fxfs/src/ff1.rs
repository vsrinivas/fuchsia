// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::crypt::UnwrappedKey,
    aes::{cipher::generic_array::GenericArray, Aes256, BlockEncrypt, NewBlockCipher},
    byteorder::{BigEndian, ByteOrder},
};

// This is a heavily specialized version of ff1 encryption as described in:
// https://nvlpubs.nist.gov/nistpubs/SpecialPublications/NIST.SP.800-38G.pdf.  This implementation
// encrypts and decrypts a u32 without a tweak. It uses radix 2.
pub struct Ff1 {
    initial_block: [u8; 16],
    cipher: Aes256,
}

impl Ff1 {
    pub fn new(key: &UnwrappedKey) -> Self {
        let cipher = Aes256::new(GenericArray::from_slice(key.key()));
        // See step 5 from the specification. radix = 2, u = 16, n = 32.
        let mut initial_block = [1, 2, 1, 0, 0, 2, 10, 16, 0, 0, 0, 32, 0, 0, 0, 0];
        cipher.encrypt_block(GenericArray::from_mut_slice(&mut initial_block));
        // initial_block is now PRF(P).
        Self { initial_block, cipher }
    }

    pub fn encrypt(&self, data: u32) -> u32 {
        // This makes assumptions that the endianness will never change, which is probably true.
        let mut a = (data >> 16) as u16;
        let mut b = data as u16;

        // ff1 uses 10 rounds.
        for i in 0..10 {
            // initial_block is PRF(P), so now we compute PRF(P || Q).
            let mut block = self.initial_block.clone();
            // xor block with Q before encrypting.
            block[13] ^= i;
            block[14] ^= (b >> 8) as u8;
            block[15] ^= b as u8;
            self.cipher.encrypt_block(GenericArray::from_mut_slice(&mut block));
            // block is now R.

            // b = 2, d = 8, so S is the first 8 bytes of block.
            let c = a.wrapping_add(BigEndian::read_u16(&block[6..8]));
            a = b;
            b = c;
        }
        (a as u32) << 16 | b as u32
    }

    // This differs from encrypt in three ways (see specification): the order of the indices is
    // reversed, the roles of A and B are swapped and modular addtiion is replaced by modular
    // subtracton.
    pub fn decrypt(&self, data: u32) -> u32 {
        let mut a = (data >> 16) as u16;
        let mut b = data as u16;

        for i in (0..10).rev() {
            // initial_block is PRF(P), so now we compute PRF(P || Q).
            let mut block = self.initial_block.clone();
            // xor block with Q before encrypting.
            block[13] ^= i;
            block[14] ^= (a >> 8) as u8;
            block[15] ^= a as u8;
            self.cipher.encrypt_block(GenericArray::from_mut_slice(&mut block));
            // block is now R.

            // b = 2, d = 8, so S is the first 8 bytes of block.
            let c = b.wrapping_sub(BigEndian::read_u16(&block[6..8]));
            b = a;
            a = c;
        }
        (a as u32) << 16 | b as u32
    }
}

#[cfg(test)]
mod tests {
    use {super::Ff1, crate::crypt::UnwrappedKey, rand};

    #[test]
    fn test_ff1() {
        // These values have been compared against other ff1 implementations.
        let ff1 = Ff1::new(&UnwrappedKey::new([0; 32]));
        assert_eq!(ff1.encrypt(1), 0x27c9468);
        assert_eq!(ff1.encrypt(999), 0x87a92dd5);

        let ff1 = Ff1::new(&UnwrappedKey::new([
            1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
            0, 1, 2,
        ]));
        assert_eq!(ff1.encrypt(1), 0x92fac14e);
        assert_eq!(ff1.encrypt(999), 0x6d2cd513);

        for _ in 0..100 {
            let v = rand::random();
            assert_eq!(ff1.decrypt(ff1.encrypt(v)), v);
        }
    }
}
