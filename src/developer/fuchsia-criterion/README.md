# Introduction

Thin wrapper around the [Criterion benchmark suite]. This library is intended to be used as both a
simple means of generating benchmark data for the dashboard, but also for local benchmarking with
Criterion.

When generating Fuchsia benchmarking results (`FuchsiaCriterion::default`), the main function where
this constructor is called will expect an output JSON path where it will store the results according
to the [Fuchsia benchmarking schema].

## Benchmarking locally

A convenient way to make use of this crate for local benchmarking and fine-tuning is to pass
`--args=local_bench='true'` to `fx set`. This will build fuchsia-criterion in a way that grants
CMD-access to Criterion's interface from the `fx shell`.

For the following example's `main.rs`:

```rust
fn fibonacci(n: u64) -> u64 {
    match n {
        0 => 1,
        1 => 1,
        n => fibonacci(n - 1) + fibonacci(n - 2),
    }
}

fn main() {
    let mut c = FuchsiaCriterion::default();
    c.bench_function("fib 10", |b| b.iter(|| fibonacci(criterion::black_box(10))));
}
```

One can run:

```shell
$ fx set PRODUCT.BOARD --args=local_bench='true'
$ fx build
$ fx shell my_fib_bench -n
```

[Criterion benchmark suite]: https://github.com/bheisler/criterion.rs
[Fuchsia benchmarking schema]: https://fuchsia.googlesource.com/fuchsia/+/HEAD/docs/development/benchmarking/results_schema.md
