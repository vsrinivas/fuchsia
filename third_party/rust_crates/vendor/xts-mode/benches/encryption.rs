#[macro_use]
extern crate criterion;

use xts_mode::{get_tweak_default, Xts128};

use aes::Aes128;
use cipher::NewBlockCipher;
use criterion::Criterion;
use rand::RngCore;

fn encryption_128(criterion: &mut Criterion) {
    let mut group = criterion.benchmark_group("xts 128");

    let mut rng = rand::thread_rng();

    let mut key = [0; 32];
    rng.fill_bytes(&mut key);

    let cipher_1 = Aes128::new_from_slice(&key[..16]).unwrap();
    let cipher_2 = Aes128::new_from_slice(&key[16..]).unwrap();

    let xts = Xts128::<Aes128>::new(cipher_1, cipher_2);

    let mut buffer = Vec::new();

    buffer.resize(16, 0);
    rng.fill_bytes(&mut buffer);
    assert_eq!(buffer.len(), 16);
    group.bench_function("sector size 16 B", |benchmark| {
        let mut i = 0;
        benchmark.iter(|| {
            let tweak = get_tweak_default(i);
            xts.encrypt_sector(&mut buffer, tweak);
            i = i.wrapping_add(1);
        })
    });

    buffer.resize(64, 0);
    rng.fill_bytes(&mut buffer);
    assert_eq!(buffer.len(), 64);
    group.bench_function("sector size 64 B", |benchmark| {
        let mut i = 0;
        benchmark.iter(|| {
            let tweak = get_tweak_default(i);
            xts.encrypt_sector(&mut buffer, tweak);
            i = i.wrapping_add(1);
        })
    });

    buffer.resize(256, 0);
    rng.fill_bytes(&mut buffer);
    assert_eq!(buffer.len(), 256);
    group.bench_function("sector size 256 B", |benchmark| {
        let mut i = 0;
        benchmark.iter(|| {
            let tweak = get_tweak_default(i);
            xts.encrypt_sector(&mut buffer, tweak);
            i = i.wrapping_add(1);
        })
    });

    buffer.resize(1024, 0);
    rng.fill_bytes(&mut buffer);
    assert_eq!(buffer.len(), 1024);
    group.bench_function("sector size 1024 B", |benchmark| {
        benchmark.iter(|| {
            xts.encrypt_area(&mut buffer, 0x200, 0, get_tweak_default);
        })
    });

    buffer.resize(8192, 0);
    rng.fill_bytes(&mut buffer);
    assert_eq!(buffer.len(), 8192);
    group.bench_function("sector size 8192 B", |benchmark| {
        benchmark.iter(|| {
            xts.encrypt_area(&mut buffer, 0x200, 0, get_tweak_default);
        })
    });

    buffer.resize(16384, 0);
    rng.fill_bytes(&mut buffer);
    assert_eq!(buffer.len(), 16384);
    group.bench_function("sector size 16384 B", |benchmark| {
        // let mut i = 0;
        benchmark.iter(|| {
            // let tweak = get_tweak_default(i);
            xts.encrypt_area(&mut buffer, 0x200, 0, get_tweak_default);
            // xts.encrypt_sector(&mut buffer, tweak);
            // i = i.wrapping_add(1);
        })
    });
}

criterion_group!(benches, encryption_128);
criterion_main!(benches);
