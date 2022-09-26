// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::framework::{Benchmark, Filesystem, OperationDuration, OperationTimer},
    async_trait::async_trait,
    fuchsia_trace as trace,
    rand::{seq::SliceRandom, Rng, SeedableRng},
    rand_xorshift::XorShiftRng,
    std::{
        fs::OpenOptions,
        io::{Seek, SeekFrom, Write},
        os::unix::io::AsRawFd,
    },
};

const RNG_SEED: u64 = 0xda782a0c3ce1819a;

/// A benchmark that measures how long `read` calls take to a file that should not already be cached
/// in memory.
#[derive(Clone)]
pub struct ReadSequentialCold {
    op_size: usize,
    op_count: usize,
}

impl ReadSequentialCold {
    pub fn new(op_size: usize, op_count: usize) -> Self {
        Self { op_size, op_count }
    }
}

#[async_trait]
impl Benchmark for ReadSequentialCold {
    async fn run(&self, fs: &mut dyn Filesystem) -> Vec<OperationDuration> {
        trace::duration!(
            "benchmark",
            "ReadSequentialCold",
            "op_size" => self.op_size as u64,
            "op_count" => self.op_count as u64
        );
        let file_path = fs.mount_point().join("file");

        // Setup
        let mut file = OpenOptions::new().write(true).create_new(true).open(&file_path).unwrap();
        write_file(&mut file, self.op_size, self.op_count);
        std::mem::drop(file);
        fs.clear_cache().await;

        // Benchmark
        let mut file = OpenOptions::new().read(true).open(&file_path).unwrap();
        read_sequential(&mut file, self.op_size, self.op_count)
    }

    fn name(&self) -> String {
        format!("ReadSequentialCold/{}", self.op_size)
    }
}

/// A benchmark that measures how long `read` calls take to a file that should already be cached in
/// memory.
#[derive(Clone)]
pub struct ReadSequentialWarm {
    op_size: usize,
    op_count: usize,
}

impl ReadSequentialWarm {
    pub fn new(op_size: usize, op_count: usize) -> Self {
        Self { op_size, op_count }
    }
}

#[async_trait]
impl Benchmark for ReadSequentialWarm {
    async fn run(&self, fs: &mut dyn Filesystem) -> Vec<OperationDuration> {
        trace::duration!(
            "benchmark",
            "ReadSequentialWarm",
            "op_size" => self.op_size as u64,
            "op_count" => self.op_count as u64
        );
        let file_path = fs.mount_point().join("file");

        // Setup
        let mut file =
            OpenOptions::new().write(true).read(true).create_new(true).open(&file_path).unwrap();
        write_file(&mut file, self.op_size, self.op_count);
        file.seek(SeekFrom::Start(0)).unwrap();

        // Benchmark
        read_sequential(&mut file, self.op_size, self.op_count)
    }

    fn name(&self) -> String {
        format!("ReadSequentialWarm/{}", self.op_size)
    }
}

/// A benchmark that measures how long random `pread` calls take to a file that should not already
/// be cached in memory.
#[derive(Clone)]
pub struct ReadRandomCold {
    op_size: usize,
    op_count: usize,
}

impl ReadRandomCold {
    pub fn new(op_size: usize, op_count: usize) -> Self {
        Self { op_size, op_count }
    }
}

#[async_trait]
impl Benchmark for ReadRandomCold {
    async fn run(&self, fs: &mut dyn Filesystem) -> Vec<OperationDuration> {
        trace::duration!(
            "benchmark",
            "ReadRandomCold",
            "op_size" => self.op_size as u64,
            "op_count" => self.op_count as u64
        );
        let file_path = fs.mount_point().join("file");

        // Setup
        let mut file = OpenOptions::new().write(true).create_new(true).open(&file_path).unwrap();
        write_file(&mut file, self.op_size, self.op_count);
        std::mem::drop(file);
        fs.clear_cache().await;

        // Benchmark
        let mut rng = XorShiftRng::seed_from_u64(RNG_SEED);
        let mut file = OpenOptions::new().read(true).open(&file_path).unwrap();
        read_random(&mut file, self.op_size, self.op_count, &mut rng)
    }

    fn name(&self) -> String {
        format!("ReadRandomCold/{}", self.op_size)
    }
}

/// A benchmark that measures how long random `pread` calls take to a file that should already be
/// cached in memory.
#[derive(Clone)]
pub struct ReadRandomWarm {
    op_size: usize,
    op_count: usize,
}

impl ReadRandomWarm {
    pub fn new(op_size: usize, op_count: usize) -> Self {
        Self { op_size, op_count }
    }
}

#[async_trait]
impl Benchmark for ReadRandomWarm {
    async fn run(&self, fs: &mut dyn Filesystem) -> Vec<OperationDuration> {
        trace::duration!(
            "benchmark",
            "ReadRandomWarm",
            "op_size" => self.op_size as u64,
            "op_count" => self.op_count as u64
        );
        let file_path = fs.mount_point().join("file");

        // Setup
        let mut file =
            OpenOptions::new().write(true).read(true).create_new(true).open(&file_path).unwrap();
        write_file(&mut file, self.op_size, self.op_count);

        // Benchmark
        let mut rng = XorShiftRng::seed_from_u64(RNG_SEED);
        read_random(&mut file, self.op_size, self.op_count, &mut rng)
    }

    fn name(&self) -> String {
        format!("ReadRandomWarm/{}", self.op_size)
    }
}

/// A benchmark that measures how long `write` calls take to a new file.
#[derive(Clone)]
pub struct WriteSequentialCold {
    op_size: usize,
    op_count: usize,
}

impl WriteSequentialCold {
    pub fn new(op_size: usize, op_count: usize) -> Self {
        Self { op_size, op_count }
    }
}

#[async_trait]
impl Benchmark for WriteSequentialCold {
    async fn run(&self, fs: &mut dyn Filesystem) -> Vec<OperationDuration> {
        trace::duration!(
            "benchmark",
            "WriteSequentialCold",
            "op_size" => self.op_size as u64,
            "op_count" => self.op_count as u64
        );
        let file_path = fs.mount_point().join("file");
        let mut file = OpenOptions::new().write(true).create_new(true).open(&file_path).unwrap();
        write_sequential(&mut file, self.op_size, self.op_count)
    }

    fn name(&self) -> String {
        format!("WriteSequentialCold/{}", self.op_size)
    }
}

/// A benchmark that measures how long `write` calls take when overwriting a file that should
/// already be in memory.
#[derive(Clone)]
pub struct WriteSequentialWarm {
    op_size: usize,
    op_count: usize,
}

impl WriteSequentialWarm {
    pub fn new(op_size: usize, op_count: usize) -> Self {
        Self { op_size, op_count }
    }
}

#[async_trait]
impl Benchmark for WriteSequentialWarm {
    async fn run(&self, fs: &mut dyn Filesystem) -> Vec<OperationDuration> {
        trace::duration!(
            "benchmark",
            "WriteSequentialWarm",
            "op_size" => self.op_size as u64,
            "op_count" => self.op_count as u64
        );
        let file_path = fs.mount_point().join("file");

        // Setup
        let mut file = OpenOptions::new().write(true).create_new(true).open(&file_path).unwrap();
        write_file(&mut file, self.op_size, self.op_count);
        file.seek(SeekFrom::Start(0)).unwrap();

        // Benchmark
        write_sequential(&mut file, self.op_size, self.op_count)
    }

    fn name(&self) -> String {
        format!("WriteSequentialWarm/{}", self.op_size)
    }
}

/// A benchmark that measures how long random `pwrite` calls take to a new file.
#[derive(Clone)]
pub struct WriteRandomCold {
    op_size: usize,
    op_count: usize,
}

impl WriteRandomCold {
    pub fn new(op_size: usize, op_count: usize) -> Self {
        Self { op_size, op_count }
    }
}

#[async_trait]
impl Benchmark for WriteRandomCold {
    async fn run(&self, fs: &mut dyn Filesystem) -> Vec<OperationDuration> {
        trace::duration!(
            "benchmark",
            "WriteRandomCold",
            "op_size" => self.op_size as u64,
            "op_count" => self.op_count as u64
        );
        let file_path = fs.mount_point().join("file");
        let mut file = OpenOptions::new().write(true).create_new(true).open(&file_path).unwrap();
        let mut rng = XorShiftRng::seed_from_u64(RNG_SEED);
        write_random(&mut file, self.op_size, self.op_count, &mut rng)
    }

    fn name(&self) -> String {
        format!("WriteRandomCold/{}", self.op_size)
    }
}

/// A benchmark that measures how long random `pwrite` calls take when overwriting a file that
/// should already be in memory.
#[derive(Clone)]
pub struct WriteRandomWarm {
    op_size: usize,
    op_count: usize,
}

impl WriteRandomWarm {
    pub fn new(op_size: usize, op_count: usize) -> Self {
        Self { op_size, op_count }
    }
}

#[async_trait]
impl Benchmark for WriteRandomWarm {
    async fn run(&self, fs: &mut dyn Filesystem) -> Vec<OperationDuration> {
        trace::duration!(
            "benchmark",
            "WriteRandomWarm",
            "op_size" => self.op_size as u64,
            "op_count" => self.op_count as u64
        );
        let file_path = fs.mount_point().join("file");

        // Setup
        let mut file = OpenOptions::new().write(true).create_new(true).open(&file_path).unwrap();
        write_sequential(&mut file, self.op_size, self.op_count);

        // Benchmark
        let mut rng = XorShiftRng::seed_from_u64(RNG_SEED);
        write_random(&mut file, self.op_size, self.op_count, &mut rng)
    }

    fn name(&self) -> String {
        format!("WriteRandomWarm/{}", self.op_size)
    }
}

fn write_file<F: Write>(file: &mut F, op_size: usize, op_count: usize) {
    let data = vec![0xAB; op_size];
    for _ in 0..op_count {
        assert_eq!(file.write(&data).unwrap(), op_size);
    }
}

/// Makes `op_count` `read` calls to `file`, each for `op_size` bytes.
fn read_sequential<F: AsRawFd>(
    file: &mut F,
    op_size: usize,
    op_count: usize,
) -> Vec<OperationDuration> {
    let mut data = vec![0; op_size];
    let mut durations = Vec::new();
    let fd = file.as_raw_fd();
    for i in 0..op_count {
        trace::duration!("benchmark", "read", "op_number" => i as u64);
        let timer = OperationTimer::start();
        let result = unsafe { libc::read(fd, data.as_mut_ptr() as *mut libc::c_void, data.len()) };
        durations.push(timer.stop());
        assert_eq!(result, op_size as isize);
    }
    durations
}

/// Makes `op_count` `write` calls to `file`, each containing `op_size` bytes.
fn write_sequential<F: AsRawFd>(
    file: &mut F,
    op_size: usize,
    op_count: usize,
) -> Vec<OperationDuration> {
    let data = vec![0xAB; op_size];
    let mut durations = Vec::new();
    let fd = file.as_raw_fd();
    for i in 0..op_count {
        trace::duration!("benchmark", "write", "op_number" => i as u64);
        let timer = OperationTimer::start();
        let result = unsafe { libc::write(fd, data.as_ptr() as *const libc::c_void, data.len()) };
        durations.push(timer.stop());
        assert_eq!(result, op_size as isize);
    }
    durations
}

fn create_random_offsets<R: Rng>(op_size: usize, op_count: usize, rng: &mut R) -> Vec<libc::off_t> {
    let op_count = op_count as libc::off_t;
    let op_size = op_size as libc::off_t;
    let mut offsets: Vec<libc::off_t> = (0..op_count).map(|offset| offset * op_size).collect();
    offsets.shuffle(rng);
    offsets
}

/// Reads the first `op_size * op_count` bytes in `file` by making `op_count` `pread` calls, each
/// `pread` call reads `op_size` bytes. The offset order for the `pread` calls is randomized using
/// `rng`.
fn read_random<F: AsRawFd, R: Rng>(
    file: &mut F,
    op_size: usize,
    op_count: usize,
    rng: &mut R,
) -> Vec<OperationDuration> {
    let offsets = create_random_offsets(op_size, op_count, rng);
    let mut data = vec![0xAB; op_size];
    let mut durations = Vec::new();
    let fd = file.as_raw_fd();
    for (i, offset) in offsets.iter().enumerate() {
        trace::duration!("benchmark", "pread", "op_number" => i as u64, "offset" => *offset);
        let timer = OperationTimer::start();
        let result =
            unsafe { libc::pread(fd, data.as_mut_ptr() as *mut libc::c_void, data.len(), *offset) };
        durations.push(timer.stop());
        assert_eq!(result, op_size as isize);
    }
    durations
}

/// Overwrites the first `op_size * op_count` bytes in `file` by making `op_count` `pwrite` calls,
/// each `pwrite` call writes `op_size` bytes. The offset order for the `pwrite` calls is
/// randomized using `rng`.
fn write_random<F: AsRawFd, R: Rng>(
    file: &mut F,
    op_size: usize,
    op_count: usize,
    rng: &mut R,
) -> Vec<OperationDuration> {
    let offsets = create_random_offsets(op_size, op_count, rng);
    let data = vec![0xAB; op_size];
    let mut durations = Vec::new();
    let fd = file.as_raw_fd();
    for (i, offset) in offsets.iter().enumerate() {
        trace::duration!("benchmark", "pwrite", "op_number" => i as u64, "offset" => *offset);
        let timer = OperationTimer::start();
        let result =
            unsafe { libc::pwrite(fd, data.as_ptr() as *const libc::c_void, data.len(), *offset) };
        durations.push(timer.stop());
        assert_eq!(result, op_size as isize);
    }
    durations
}

#[cfg(test)]
mod tests {

    use {
        super::*,
        crate::framework::{filesystem::Memfs, Filesystem},
        futures::lock::Mutex,
        std::sync::Arc,
    };

    const OP_SIZE: usize = 8;
    const OP_COUNT: usize = 2;

    /// Filesystem implementation that records `clear_cache` calls for validating warm and cold
    /// benchmarks.
    #[derive(Clone)]
    struct TestFilesystem {
        inner: Arc<Mutex<TestFilesystemInner>>,
    }

    struct TestFilesystemInner {
        memfs: Option<Box<Memfs>>,
        clear_cache_count: u64,
    }

    impl TestFilesystem {
        async fn new() -> Self {
            Self {
                inner: Arc::new(Mutex::new(TestFilesystemInner {
                    memfs: Some(Box::new(Memfs::new().await)),
                    clear_cache_count: 0,
                })),
            }
        }
    }

    #[async_trait]
    impl Filesystem for TestFilesystem {
        async fn clear_cache(&mut self) {
            let mut inner = self.inner.lock().await;
            inner.clear_cache_count += 1;
            inner.memfs.as_mut().unwrap().clear_cache().await;
        }

        async fn shutdown(self: Box<Self>) {
            let mut inner = self.inner.lock().await;
            inner.memfs.take().unwrap().shutdown().await;
        }
    }

    async fn check_benchmark(benchmark: impl Benchmark, op_count: usize, clear_cache_count: u64) {
        let mut test_fs = Box::new(TestFilesystem::new().await);
        let results = benchmark.run(test_fs.as_mut()).await;

        assert_eq!(results.len(), op_count);
        {
            let stats = test_fs.inner.lock().await;
            assert_eq!(stats.clear_cache_count, clear_cache_count);
        }
        test_fs.shutdown().await;
    }

    #[fuchsia::test]
    async fn read_sequential_cold_test() {
        check_benchmark(
            ReadSequentialCold::new(OP_SIZE, OP_COUNT),
            OP_COUNT,
            /*clear_cache_count=*/ 1,
        )
        .await;
    }

    #[fuchsia::test]
    async fn read_sequential_warm_test() {
        check_benchmark(
            ReadSequentialWarm::new(OP_SIZE, OP_COUNT),
            OP_COUNT,
            /*clear_cache_count=*/ 0,
        )
        .await;
    }

    #[fuchsia::test]
    async fn read_random_cold_test() {
        check_benchmark(
            ReadRandomCold::new(OP_SIZE, OP_COUNT),
            OP_COUNT,
            /*clear_cache_count=*/ 1,
        )
        .await;
    }

    #[fuchsia::test]
    async fn read_random_warm_test() {
        check_benchmark(
            ReadRandomWarm::new(OP_SIZE, OP_COUNT),
            OP_COUNT,
            /*clear_cache_count=*/ 0,
        )
        .await;
    }

    #[fuchsia::test]
    async fn write_sequential_cold_test() {
        check_benchmark(
            WriteSequentialCold::new(OP_SIZE, OP_COUNT),
            OP_COUNT,
            /*clear_cache_count=*/ 0,
        )
        .await;
    }

    #[fuchsia::test]
    async fn write_sequential_warm_test() {
        check_benchmark(
            WriteSequentialWarm::new(OP_SIZE, OP_COUNT),
            OP_COUNT,
            /*clear_cache_count=*/ 0,
        )
        .await;
    }

    #[fuchsia::test]
    async fn write_random_cold_test() {
        check_benchmark(
            WriteRandomCold::new(OP_SIZE, OP_COUNT),
            OP_COUNT,
            /*clear_cache_count=*/ 0,
        )
        .await;
    }

    #[fuchsia::test]
    async fn write_random_warm_test() {
        check_benchmark(
            WriteRandomWarm::new(OP_SIZE, OP_COUNT),
            OP_COUNT,
            /*clear_cache_count=*/ 0,
        )
        .await;
    }
}
