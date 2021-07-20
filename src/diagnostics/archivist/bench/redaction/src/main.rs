// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_criterion::{
    criterion::{self, Criterion},
    FuchsiaCriterion,
};

use archivist_lib::logs::redact::Redactor;
use std::mem;
use std::time::Duration;

fn main() {
    let mut c = FuchsiaCriterion::default();
    let internal_c: &mut Criterion = &mut c;
    *internal_c = mem::take(internal_c)
        .warm_up_time(Duration::from_millis(100))
        .measurement_time(Duration::from_millis(250))
        .sample_size(20);

    const NO_MATCH: &'static str =
        "[1023.223] INFO: This is a log message without sensitive information";
    const WITH_MATCH: &'static str =
        "[1023.223] INFO: This is a log message with a MAC: 00:0a:95:9F:68:16";
    const MULTI_MATCH: &'static str =
        "[1023.223] INFO: alice@website.tld 8.8.8.8 127.0.0.1 abc@example.com";

    let mut bench = criterion::Benchmark::new("Redactor/Noop/NO_MATCH", move |b| {
        let r = Redactor::noop();
        b.iter(|| criterion::black_box(r.redact_text(NO_MATCH)));
    });

    bench = bench.with_function("Redactor/Noop/WITH_MATCH", move |b| {
        let r = Redactor::noop();
        b.iter(|| criterion::black_box(r.redact_text(WITH_MATCH)));
    });

    bench = bench.with_function("Redactor/Noop/MULTI_MATCH", move |b| {
        let r = Redactor::noop();
        b.iter(|| criterion::black_box(r.redact_text(MULTI_MATCH)));
    });

    bench = bench.with_function("Redactor/StaticPatterns/NO_MATCH", move |b| {
        let r = Redactor::with_static_patterns();
        b.iter(|| criterion::black_box(r.redact_text(NO_MATCH)));
    });

    bench = bench.with_function("Redactor/StaticPatterns/WITH_MATCH", move |b| {
        let r = Redactor::with_static_patterns();
        b.iter(|| criterion::black_box(r.redact_text(WITH_MATCH)));
    });

    bench = bench.with_function("Redactor/StaticPatterns/MULTI_MATCH", move |b| {
        let r = Redactor::with_static_patterns();
        b.iter(|| criterion::black_box(r.redact_text(MULTI_MATCH)));
    });

    c.bench("fuchsia.archivist", bench);
}
