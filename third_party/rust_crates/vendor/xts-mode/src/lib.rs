/*!
[XTS block mode](https://en.wikipedia.org/wiki/Disk_encryption_theory#XEX-based_tweaked-codebook_mode_with_ciphertext_stealing_(XTS)) implementation in rust.
Currently only 128-bit (16-byte) algorithms are supported, if you require other
sizes, please open an issue. Note that AES-256 uses 128-bit blocks, so it should
work as is.

# Examples:

Encrypting and decrypting multiple sectors at a time:
```
use aes::{Aes128, NewBlockCipher, cipher::generic_array::GenericArray};
use xts_mode::{Xts128, get_tweak_default};

// Load the encryption key
let key = [1; 32];
let plaintext = [5; 0x400];

// Load the data to be encrypted
let mut buffer = plaintext.to_owned();

let cipher_1 = Aes128::new(GenericArray::from_slice(&key[..16]));
let cipher_2 = Aes128::new(GenericArray::from_slice(&key[16..]));

let xts = Xts128::<Aes128>::new(cipher_1, cipher_2);

let sector_size = 0x200;
let first_sector_index = 0;

// Encrypt data in the buffer
xts.encrypt_area(&mut buffer, sector_size, first_sector_index, get_tweak_default);

// Decrypt data in the buffer
xts.decrypt_area(&mut buffer, sector_size, first_sector_index, get_tweak_default);

assert_eq!(&buffer[..], &plaintext[..]);
```

Encrypting and decrypting a single sector:
```
use aes::{Aes128, NewBlockCipher, cipher::generic_array::GenericArray};
use xts_mode::{Xts128, get_tweak_default};

// Load the encryption key
let key = [1; 32];
let plaintext = [5; 0x200];

// Load the data to be encrypted
let mut buffer = plaintext.to_owned();

let cipher_1 = Aes128::new(GenericArray::from_slice(&key[..16]));
let cipher_2 = Aes128::new(GenericArray::from_slice(&key[16..]));

let xts = Xts128::<Aes128>::new(cipher_1, cipher_2);

let tweak = get_tweak_default(0); // 0 is the sector index

// Encrypt data in the buffer
xts.encrypt_sector(&mut buffer, tweak);

// Decrypt data in the buffer
xts.decrypt_sector(&mut buffer, tweak);

assert_eq!(&buffer[..], &plaintext[..]);
```

Decrypting a [NCA](https://switchbrew.org/wiki/NCA_Format) (nintendo content archive) header:
```
use aes::{Aes128, NewBlockCipher, cipher::generic_array::GenericArray};
use xts_mode::{Xts128, get_tweak_default};

pub fn get_nintendo_tweak(sector_index: u128) -> [u8; 0x10] {
    sector_index.to_be_bytes()
}

// Load the header key
let header_key = &[0; 0x20];

// Read into buffer header to be decrypted
let mut buffer = vec![0; 0xC00];

let cipher_1 = Aes128::new(GenericArray::from_slice(&header_key[..0x10]));
let cipher_2 = Aes128::new(GenericArray::from_slice(&header_key[0x10..]));

let mut xts = Xts128::new(cipher_1, cipher_2);

// Decrypt the first 0x400 bytes of the header in 0x200 sections
xts.decrypt_area(&mut buffer[0..0x400], 0x200, 0, get_nintendo_tweak);

let magic = &buffer[0x200..0x204];
# if false { // Removed from tests as we're not decrypting an actual NCA
assert_eq!(magic, b"NCA3"); // In older NCA versions the section index used in header encryption was different
# }

// Decrypt the rest of the header
xts.decrypt_area(&mut buffer[0x400..0xC00], 0x200, 2, get_nintendo_tweak);
```
*/

use std::convert::TryFrom;
use std::convert::TryInto;

use byteorder::{ByteOrder, LittleEndian};
use cipher::generic_array::typenum::Unsigned;
use cipher::generic_array::GenericArray;
use cipher::{BlockCipher, BlockDecrypt, BlockEncrypt};

/// Xts128 block cipher. Does not implement implement BlockMode due to XTS differences detailed
/// [here](https://github.com/RustCrypto/block-ciphers/issues/48#issuecomment-574440662).
pub struct Xts128<C: BlockEncrypt + BlockDecrypt + BlockCipher> {
    /// This cipher is actually used to encrypt the blocks.
    cipher_1: C,
    /// This cipher is used only to compute the tweak at each sector start.
    cipher_2: C,
}

impl<C: BlockEncrypt + BlockDecrypt + BlockCipher> Xts128<C> {
    /// Creates a new Xts128 using two cipher instances: the first one's used to encrypt the blocks
    /// and the second one to compute the tweak at the start of each sector.
    ///
    /// Usually both cipher's are the same algorithm and the key is stored as double sized
    /// (256 bits for Aes128), and the key is split in half, the first half used for cipher_1 and
    /// the other for cipher_2.
    ///
    /// If you require support for different cipher types, or block sizes different than 16 bytes,
    /// open an issue.
    pub fn new(cipher_1: C, cipher_2: C) -> Xts128<C> {
        Xts128 { cipher_1, cipher_2 }
    }

    /// Encrypts a single sector in place using the given tweak.
    ///
    /// # Panics
    /// - If the block size is not 16 bytes.
    /// - If there's less than a single block in the sector.
    pub fn encrypt_sector(&self, sector: &mut [u8], mut tweak: [u8; 16]) {
        assert_eq!(
            <C as BlockCipher>::BlockSize::to_usize(),
            128 / 8,
            "Wrong block size"
        );

        assert!(
            sector.len() >= 16,
            "AES-XTS needs at least two blocks to perform stealing, or a single complete block"
        );

        let block_count = sector.len() / 16;
        let need_stealing = sector.len() % 16 != 0;

        // Compute tweak
        self.cipher_2
            .encrypt_block(GenericArray::from_mut_slice(&mut tweak));

        let nosteal_block_count = if need_stealing {
            block_count - 1
        } else {
            block_count
        };

        for i in (0..sector.len()).step_by(16).take(nosteal_block_count) {
            let block = &mut sector[i..i + 16];

            xor(block, &tweak);
            self.cipher_1
                .encrypt_block(GenericArray::from_mut_slice(block));
            xor(block, &tweak);

            tweak = galois_field_128_mul_le(tweak);
        }

        if need_stealing {
            let next_to_last_tweak = tweak;
            let last_tweak = galois_field_128_mul_le(tweak);

            let remaining = sector.len() % 16;
            let mut block: [u8; 16] = sector[16 * (block_count - 1)..16 * block_count]
                .try_into()
                .unwrap();

            xor(&mut block, &next_to_last_tweak);
            self.cipher_1
                .encrypt_block(GenericArray::from_mut_slice(&mut block));
            xor(&mut block, &next_to_last_tweak);

            let mut last_block = [0u8; 16];
            last_block[..remaining].copy_from_slice(&sector[16 * block_count..]);
            last_block[remaining..].copy_from_slice(&block[remaining..]);

            xor(&mut last_block, &last_tweak);
            self.cipher_1
                .encrypt_block(GenericArray::from_mut_slice(&mut last_block));
            xor(&mut last_block, &last_tweak);

            sector[16 * (block_count - 1)..16 * block_count].copy_from_slice(&last_block);
            sector[16 * block_count..].copy_from_slice(&block[..remaining]);
        }
    }

    /// Decrypts a single sector in place using the given tweak.
    ///
    /// # Panics
    /// - If the block size is not 16 bytes.
    /// - If there's less than a single block in the sector.
    pub fn decrypt_sector(&self, sector: &mut [u8], mut tweak: [u8; 16]) {
        assert_eq!(
            <C as BlockCipher>::BlockSize::to_usize(),
            128 / 8,
            "Wrong block size"
        );

        assert!(
            sector.len() >= 16,
            "AES-XTS needs at least two blocks to perform stealing, or a single complete block"
        );

        let block_count = sector.len() / 16;
        let need_stealing = sector.len() % 16 != 0;

        // Compute tweak
        self.cipher_2
            .encrypt_block(GenericArray::from_mut_slice(&mut tweak));

        let nosteal_block_count = if need_stealing {
            block_count - 1
        } else {
            block_count
        };

        for i in (0..sector.len()).step_by(16).take(nosteal_block_count) {
            let block = &mut sector[i..i + 16];

            xor(block, &tweak);
            self.cipher_1
                .decrypt_block(GenericArray::from_mut_slice(block));
            xor(block, &tweak);

            tweak = galois_field_128_mul_le(tweak);
        }

        if need_stealing {
            let next_to_last_tweak = tweak;
            let last_tweak = galois_field_128_mul_le(tweak);

            let remaining = sector.len() % 16;
            let mut block: [u8; 16] = sector[16 * (block_count - 1)..16 * block_count]
                .try_into()
                .unwrap();

            xor(&mut block, &last_tweak);
            self.cipher_1
                .decrypt_block(GenericArray::from_mut_slice(&mut block));
            xor(&mut block, &last_tweak);

            let mut last_block = [0u8; 16];
            last_block[..remaining].copy_from_slice(&sector[16 * block_count..]);
            last_block[remaining..].copy_from_slice(&block[remaining..]);

            xor(&mut last_block, &next_to_last_tweak);
            self.cipher_1
                .decrypt_block(GenericArray::from_mut_slice(&mut last_block));
            xor(&mut last_block, &next_to_last_tweak);

            sector[16 * (block_count - 1)..16 * block_count].copy_from_slice(&last_block);
            sector[16 * block_count..].copy_from_slice(&block[..remaining]);
        }
    }

    /// Encrypts a whole area in place, usually consisting of multiple sectors.
    ///
    /// The tweak is computed at the start of every sector using get_tweak_fn(sector_index).
    /// `get_tweak_fn` is usually `get_tweak_default`.
    ///
    /// # Panics
    /// - If the block size is not 16 bytes.
    /// - If there's less than a single block in the last sector.
    pub fn encrypt_area(
        &self,
        area: &mut [u8],
        sector_size: usize,
        first_sector_index: u128,
        get_tweak_fn: impl Fn(u128) -> [u8; 16],
    ) {
        let area_len = area.len();
        let mut chunks = area.chunks_exact_mut(sector_size);
        for (i, chunk) in (&mut chunks).enumerate() {
            let tweak = get_tweak_fn(
                u128::try_from(i).expect("usize cannot be bigger than u128") + first_sector_index,
            );
            self.encrypt_sector(chunk, tweak);
        }
        let remainder = chunks.into_remainder();

        if !remainder.is_empty() {
            let i = area_len / sector_size;
            let tweak = get_tweak_fn(
                u128::try_from(i).expect("usize cannot be bigger than u128") + first_sector_index,
            );
            self.encrypt_sector(remainder, tweak);
        }
    }

    /// Decrypts a whole area in place, usually consisting of multiple sectors.
    ///
    /// The tweak is computed at the start of every sector using get_tweak_fn(sector_index).
    /// `get_tweak_fn` is usually `get_tweak_default`.
    ///
    /// # Panics
    /// - If the block size is not 16 bytes.
    /// - If there's less than a single block in the last sector.
    pub fn decrypt_area(
        &self,
        area: &mut [u8],
        sector_size: usize,
        first_sector_index: u128,
        get_tweak_fn: impl Fn(u128) -> [u8; 16],
    ) {
        let area_len = area.len();
        let mut chunks = area.chunks_exact_mut(sector_size);
        for (i, chunk) in (&mut chunks).enumerate() {
            let tweak = get_tweak_fn(
                u128::try_from(i).expect("usize cannot be bigger than u128") + first_sector_index,
            );
            self.decrypt_sector(chunk, tweak);
        }
        let remainder = chunks.into_remainder();

        if !remainder.is_empty() {
            let i = area_len / sector_size;
            let tweak = get_tweak_fn(
                u128::try_from(i).expect("usize cannot be bigger than u128") + first_sector_index,
            );
            self.decrypt_sector(remainder, tweak);
        }
    }
}

/// This is the default way to get the tweak, which just consists of separating the sector_index
/// in an array of 16 bytes with little endian. May be called to get the tweak for every sector
/// or passed directly to `(en/de)crypt_area`, which will basically do that.
pub fn get_tweak_default(sector_index: u128) -> [u8; 16] {
    sector_index.to_le_bytes()
}

#[inline(always)]
fn xor(buf: &mut [u8], key: &[u8]) {
    debug_assert_eq!(buf.len(), key.len());
    for (a, b) in buf.iter_mut().zip(key) {
        *a ^= *b;
    }
}

fn galois_field_128_mul_le(tweak_source: [u8; 16]) -> [u8; 16] {
    let low_bytes = u64::from_le_bytes(tweak_source[0..8].try_into().unwrap());
    let high_bytes = u64::from_le_bytes(tweak_source[8..16].try_into().unwrap());
    let new_low_bytes = (low_bytes << 1) ^ if (high_bytes >> 63) != 0 { 0x87 } else { 0x00 };
    let new_high_bytes = (low_bytes >> 63) | (high_bytes << 1);

    let mut tweak = [0; 16];

    // byteorder used for performance, as it uses std::ptr::copy_nonoverlapping
    LittleEndian::write_u64(&mut tweak[0..8], new_low_bytes);
    LittleEndian::write_u64(&mut tweak[8..16], new_high_bytes);

    tweak
}
