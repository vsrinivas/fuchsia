#![feature(test)]

extern crate vte;
extern crate test;

use test::{black_box};

struct BlackBox;

impl vte::Perform for BlackBox {
    fn print(&mut self, c: char) {
        black_box(c);
    }

    fn execute(&mut self, byte: u8) {
        black_box(byte);
    }

    fn hook(&mut self, params: &[i64], intermediates: &[u8], ignore: bool) {
        black_box(params);
        black_box(intermediates);
        black_box(ignore);
    }

    fn put(&mut self, byte: u8) {
        black_box(byte);
    }

    fn unhook(&mut self) {
        black_box("unhook");
    }

    fn osc_dispatch(&mut self, params: &[&[u8]]) {
        black_box(params);
    }

    fn csi_dispatch(&mut self, params: &[i64], intermediates: &[u8], ignore: bool, c: char) {
        black_box(params);
        black_box(intermediates);
        black_box(ignore);
        black_box(c);
    }

    fn esc_dispatch(&mut self, params: &[i64], intermediates: &[u8], ignore: bool, byte: u8) {
        black_box(params);
        black_box(intermediates);
        black_box(ignore);
        black_box(byte);
    }
}

/// Large vim scrolling
fn main() {
    let buffer = include_bytes!("../benches/large_vim_scroll.recording");
    let bytes = buffer.len();
    let target = 100 * 1024 * 1024;
    let iterations = target / bytes;

    let (mut state, mut parser) = (BlackBox, vte::Parser::new());

    for _ in 0..iterations {
        for byte in &buffer[..] {
            parser.advance(&mut state, *byte);
        }
    }
}

