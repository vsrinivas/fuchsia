#![feature(test)]

extern crate lipsum;
extern crate test;

use test::Bencher;

#[bench]
fn generate_lorem_ipsum_100(b: &mut Bencher) {
    b.iter(|| lipsum::lipsum(100))
}

#[bench]
fn generate_lorem_ipsum_200(b: &mut Bencher) {
    b.iter(|| lipsum::lipsum(200))
}
