// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::global_executor,
    fidl_fidl_benchmarks::BindingsUnderTestProxy,
    futures::future::Future,
    std::sync::Arc,
    std::time::{Duration, Instant},
};

trait BenchmarkFnTrait {
    fn run(&self, proxy: BindingsUnderTestProxy, size: usize, concurrency: usize) -> Duration;
    fn sizes(&self) -> Vec<usize>;
}

struct BenchmarkFnNoSetup<F, T>
where
    F: Future<Output = T> + Send,
{
    func: Arc<dyn Fn(BindingsUnderTestProxy, usize) -> F>,
    sizes: Vec<usize>,
}

impl<F, T> BenchmarkFnTrait for BenchmarkFnNoSetup<F, T>
where
    F: Future<Output = T> + Send,
{
    fn run(&self, proxy: BindingsUnderTestProxy, size: usize, concurrency: usize) -> Duration {
        let timer = Instant::now();
        global_executor::run_singlethreaded(async {
            let futures: Vec<F> =
                (0..concurrency).map(|_| (self.func)(proxy.clone(), size)).collect();
            futures::future::join_all(futures).await
        });
        timer.elapsed()
    }
    fn sizes(&self) -> Vec<usize> {
        self.sizes.clone()
    }
}

struct BenchmarkFnWithSetup<F, I, O>
where
    F: Future<Output = O> + Send,
{
    setup: Arc<dyn Fn(usize) -> I>,
    func: Arc<dyn Fn(I, BindingsUnderTestProxy) -> F>,
    sizes: Vec<usize>,
}

impl<F, I, O> BenchmarkFnTrait for BenchmarkFnWithSetup<F, I, O>
where
    F: Future<Output = O> + Send,
{
    fn run(&self, proxy: BindingsUnderTestProxy, size: usize, concurrency: usize) -> Duration {
        let inputs: Vec<I> = (0..concurrency).map(|_| (self.setup)(size)).collect();
        let timer = Instant::now();

        global_executor::run_singlethreaded(async {
            let futures: Vec<F> =
                inputs.into_iter().map(|input| (self.func)(input, proxy.clone())).collect();
            futures::future::join_all(futures).await
        });
        timer.elapsed()
    }
    fn sizes(&self) -> Vec<usize> {
        self.sizes.clone()
    }
}

pub struct BenchmarkFn {
    name: String,
    format_size_fn: Arc<dyn Fn(usize) -> String>,
    func: Box<dyn BenchmarkFnTrait>,
}

impl BenchmarkFn {
    pub fn new<N, FS, B, F, O, I, S>(
        name: N,
        format_size: FS,
        sizes: Vec<usize>,
        setup: S,
        func: B,
    ) -> BenchmarkFn
    where
        N: Into<String>,
        FS: 'static + Fn(usize) -> String,
        F: Future<Output = O> + Send + 'static,
        O: 'static,
        I: 'static,
        S: 'static + Fn(usize) -> I,
        B: 'static + Fn(I, BindingsUnderTestProxy) -> F,
    {
        BenchmarkFn {
            name: name.into(),
            format_size_fn: Arc::new(format_size),
            func: Box::new(BenchmarkFnWithSetup {
                setup: Arc::new(setup),
                func: Arc::new(func),
                sizes,
            }),
        }
    }

    pub fn run(&self, proxy: BindingsUnderTestProxy, size: usize, concurrency: usize) -> Duration {
        self.func.run(proxy, size, concurrency)
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn format_size(&self, size: usize) -> String {
        (self.format_size_fn)(size)
    }

    pub fn sizes(&self) -> Vec<usize> {
        self.func.sizes()
    }
}
