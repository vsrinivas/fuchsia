//! Parse input from stdin and log actions on stdout
extern crate vte;

use std::io::{self, Read};

/// A type implementing Perform that just logs actions
struct Log;

impl vte::Perform for Log {
    fn print(&mut self, c: char) {
        println!("[print] {:?}", c);
    }

    fn execute(&mut self, byte: u8) {
        println!("[execute] {:02x}", byte);
    }

    fn hook(&mut self, params: &[i64], intermediates: &[u8], ignore: bool) {
        println!("[hook] params={:?}, intermediates={:?}, ignore={:?}",
                 params, intermediates, ignore);
    }

    fn put(&mut self, byte: u8) {
        println!("[put] {:02x}", byte);
    }

    fn unhook(&mut self) {
        println!("[unhook]");
    }

    fn osc_dispatch(&mut self, params: &[&[u8]]) {
        println!("[osc_dispatch] params={:?}", params);
    }

    fn csi_dispatch(&mut self, params: &[i64], intermediates: &[u8], ignore: bool, c: char) {
        println!("[csi_dispatch] params={:?}, intermediates={:?}, ignore={:?}, char={:?}",
                 params, intermediates, ignore, c);
    }

    fn esc_dispatch(&mut self, params: &[i64], intermediates: &[u8], ignore: bool, byte: u8) {
        println!("[esc_dispatch] params={:?}, intermediates={:?}, ignore={:?}, byte={:02x}",
                 params, intermediates, ignore, byte);
    }

}

fn main() {
    let input = io::stdin();
    let mut handle = input.lock();

    let mut statemachine = vte::Parser::new();
    let mut parser = Log;

    let mut buf: [u8; 2048] = unsafe { std::mem::uninitialized() };

    loop {
        match handle.read(&mut buf) {
            Ok(0) => break,
            Ok(n) => {
                for byte in &buf[..n] {
                    statemachine.advance(&mut parser, *byte);
                }
            },
            Err(err) => {
                println!("err: {}", err);
                break;
            }
        }
    }
}
