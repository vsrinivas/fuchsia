#![feature(test)]

extern crate lipsum;
extern crate test;

use test::Bencher;

#[bench]
fn learn_lorem_ipsum(b: &mut Bencher) {
    b.iter(|| {
        let mut chain = lipsum::MarkovChain::new();
        chain.learn(lipsum::LOREM_IPSUM)
    })
}

#[bench]
fn learn_liber_primus(b: &mut Bencher) {
    b.iter(|| {
        let mut chain = lipsum::MarkovChain::new();
        chain.learn(lipsum::LIBER_PRIMUS)
    })
}
