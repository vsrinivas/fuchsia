// TODO: write a test that makes use of the AES-256-XTS bindings with some test vectors

#![allow(elided_lifetimes_in_paths)]

use fcrypto_rust::Aes256XtsCipher;

#[test]
fn test_cipher() {
  // TODO: double-check these lengths
  let key = [0u8; 32];
  let iv = [0u8; 32];
  let mut cipher = Aes256XtsCipher::new(&key, &iv).expect("create and init cipher");
  let ctext = [0u8 ; 128];
  let mut ptext = [0u8 ; 128];
  cipher.encrypt(0, &ctext, &mut ptext).expect("encrypt");
}

