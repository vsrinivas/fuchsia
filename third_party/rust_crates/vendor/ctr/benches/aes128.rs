#![feature(test)]

cipher::stream_cipher_sync_bench!(ctr::Ctr128<aes::Aes128>);
