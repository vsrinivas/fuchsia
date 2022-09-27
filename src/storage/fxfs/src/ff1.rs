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
        // These values have been compared against the implementation in the Rust binary-ff1 crate.
        // The rust binary-ff1 crate will produce the same results if the bits in each byte are
        // reversed on the input and likewise on the output.
        let ff1 = Ff1::new(&UnwrappedKey::new([0; 32]));
        assert_eq!(ff1.encrypt(1), 0x27c9468);
        assert_eq!(ff1.encrypt(999), 0x87a92dd5);
        assert_eq!(ff1.encrypt(11471928), 0x70c679b1);
        assert_eq!(ff1.encrypt(318689559), 0xdec5199a);

        let ff1 = Ff1::new(&UnwrappedKey::new([
            1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
            0, 1, 2,
        ]));
        assert_eq!(ff1.encrypt(1), 0x92fac14e);
        assert_eq!(ff1.encrypt(999), 0x6d2cd513);
        assert_eq!(ff1.encrypt(11471928), 0xdb672d05);
        assert_eq!(ff1.encrypt(318689559), 0x66d58b7e);

        let ff1 = Ff1::new(&UnwrappedKey::new([
            0xf8, 0x24, 0x6b, 0x2c, 0x38, 0x39, 0xfa, 0x6d, 0x98, 0xe8, 0x56, 0x17, 0x0c, 0xdd,
            0xf4, 0xf1, 0x1b, 0xa5, 0xa6, 0xcb, 0x07, 0x06, 0x58, 0x4c, 0x2a, 0x63, 0x9d, 0x32,
            0x22, 0x80, 0xe6, 0xf1,
        ]));
        assert_eq!(ff1.encrypt(1), 0xfc049c96);
        assert_eq!(ff1.encrypt(999), 0xbe10a02a);
        assert_eq!(ff1.encrypt(11471928), 0xe1290afd);
        assert_eq!(ff1.encrypt(318689559), 0xf4fcf414);

        for _ in 0..100 {
            let v = rand::random();
            assert_eq!(ff1.decrypt(ff1.encrypt(v)), v);
        }
    }
}
