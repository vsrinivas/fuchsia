# xts-mode

[XTS block mode](https://en.wikipedia.org/wiki/Disk_encryption_theory#XEX-based_tweaked-codebook_mode_with_ciphertext_stealing_(XTS)) implementation in rust.
Currently only 128-bit (16-byte) algorithms are supported, if you require other
sizes, please open an issue. Note that AES-256 uses 128-bit blocks, so it should
work as is.

## Examples:

Encrypting and decrypting multiple sectors at a time:
```rust
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
```rust
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
```rust
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
assert_eq!(magic, b"NCA3"); // In older NCA versions the section index used in header encryption was different

// Decrypt the rest of the header
xts.decrypt_area(&mut buffer[0x400..0xC00], 0x200, 2, get_nintendo_tweak);
```
