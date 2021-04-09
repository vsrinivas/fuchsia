// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_criterion::{
    criterion::{self, Criterion},
    FuchsiaCriterion,
};

use selectors;
use std::mem;
use std::time::Duration;

struct Case {
    name: &'static str,
    val: String,
}

impl Case {
    fn to_string(&self) -> String {
        format!("{}/{}", self.name, self.val.len())
    }
}

fn make_repeated_cases(name: &'static str, base: &'static str, repeats: Vec<usize>) -> Vec<Case> {
    let mut ret = vec![];
    for r in repeats {
        ret.push(Case { name: name, val: base.repeat(r) });
    }
    ret
}

fn main() {
    let mut c = FuchsiaCriterion::default();
    let internal_c: &mut Criterion = &mut c;
    *internal_c = mem::take(internal_c)
        .warm_up_time(Duration::from_millis(150))
        .measurement_time(Duration::from_millis(300))
        .sample_size(20);

    let mut bench = criterion::Benchmark::new("sanitize_string_for_selectors/empty", move |b| {
        b.iter(|| criterion::black_box(selectors::sanitize_string_for_selectors("")));
    });

    // Measure the time taken by sanitize_string_for_selectors() on
    // strings where different amounts of string escaping is required. This
    // function is called frequently during selector parsing.
    let cases: Vec<Case> = vec![]
        .into_iter()
        .chain(make_repeated_cases("no_replace", "abcd", vec![2, 64]).into_iter())
        .chain(make_repeated_cases("replace_half", "a:b*", vec![2, 64]).into_iter())
        .chain(make_repeated_cases("replace_all", ":*\\:", vec![2, 64]).into_iter())
        .collect();

    for case in cases.into_iter() {
        bench = bench.with_function(
            format!("sanitize_string_for_selectors/{}", case.to_string()),
            move |b| {
                b.iter(|| criterion::black_box(selectors::sanitize_string_for_selectors(&case.val)))
            },
        );
    }

    c.bench("fuchsia.diagnostics.lib.selectors", bench);
}
