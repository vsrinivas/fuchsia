// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::BenchmarkFn;

static ASCII: &str = "Jived fox nymph grabs quick waltz.
Glib jocks quiz nymph to vex dwarf.
Sphinx of black quartz, judge my vow.
The five boxing wizards jump quickly.";

static UNICODE: &str = "以呂波耳本部止
千利奴流乎和加
餘多連曽津祢那
良牟有為能於久
耶万計不己衣天
阿佐伎喩女美之
恵比毛勢須";

// Make a valid utf8 string of with the byte length specified based on repeating the supplied characters.
fn make_string(characters: &'static str, length: usize) -> String {
    let mut bytes: Vec<u8> =
        String::from(characters).as_bytes().iter().cycle().take(length).copied().collect();
    // Make sure the end of the string is valid utf8 by replacing partial non-ASCII characters with spaces.
    for i in (0..length).rev() {
        let b: u8 = bytes[i];
        if b & 0b10000000 == 0 {
            break;
        }
        bytes[i] = 0x20;
        if b & 0b11000000 == 0b11000000 {
            break;
        }
    }
    String::from_utf8(bytes).unwrap()
}

fn string_benchmark(name: &'static str, characters: &'static str) -> BenchmarkFn {
    BenchmarkFn::new(
        format!("{}String", name),
        |length| format!("Len{}", length),
        vec![16, 256, 4096],
        move |length| make_string(characters, length),
        |s, proxy| async move {
            let call_fut = proxy.echo_string(&s);
            call_fut.await.unwrap()
        },
    )
}

fn string_vector_benchmark(name: &'static str, characters: &'static str) -> BenchmarkFn {
    BenchmarkFn::new(
        format!("{}StringVector", name),
        |length| format!("{}Elems{}Strings", length, 4096 / length),
        vec![16, 256],
        move |vector_length| {
            let string_length = 4096 / vector_length;
            let message_length = 16 + vector_length * (16 + std::cmp::max(string_length, 8));
            assert!(message_length < 0xFFFF);
            let s = make_string(characters, string_length);
            vec![s].iter().cycle().take(vector_length).cloned().collect::<Vec<String>>()
        },
        |v, proxy| async move {
            let call_fut = proxy.echo_strings(&mut v.iter().map(|s| s.as_str()));
            call_fut.await.unwrap()
        },
    )
}

pub fn all() -> Vec<BenchmarkFn> {
    vec![
        string_benchmark("Ascii", ASCII),
        string_benchmark("Unicode", UNICODE),
        string_vector_benchmark("Ascii", ASCII),
        string_vector_benchmark("Unicode", UNICODE),
    ]
}
