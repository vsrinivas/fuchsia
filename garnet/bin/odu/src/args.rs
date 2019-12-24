// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::common_operations::allowed_ops,
    crate::target::{AvailableTargets, TargetOps},
    clap::{App, Arg},
    log::error,
    std::ops::RangeInclusive,
    thiserror::Error,
};

#[derive(Debug, Error, PartialEq)]
pub enum Error {
    #[error("Operation not supported for the target.")]
    OperationNotSupported,
}

#[derive(Debug)]
pub struct ParseArgs {
    /// Limit on number of outstanding IOs - IOs that are generated but are not
    /// complete.
    pub queue_depth: usize,

    /// odu writes a header at the beginning of each block boundary. When not
    /// passed as a parameter an attempt may be made to guess the block size. On
    /// failure to get block size, a default block size is chosen. This helps
    /// odu to verify the success of the operation. See also `align` and
    /// `max_io_size`.
    pub block_size: u64,

    /// Maximum size of the IO issued. This parameter specifies the number of
    /// bytes read/written from/to the `target` in one operation.
    pub max_io_size: u64,

    /// If set to true, IOs are aligned to `block_size`. If set to false, a
    /// random offset is chosen to issue IOs.
    pub align: bool,

    /// Number of IO operations to generate. This number does not include IOs
    /// issued to verify.
    pub max_io_count: u64,

    /// IOs are issued on the `target` for a range of offset between
    /// [0, `target_length`). The bytes from offset `target_length` till end of
    /// target are not to operated on.
    pub target_length: u64,

    /// Configures number of IO issuing threads. IO issuing threads are
    /// generally not cpu bound. There may be more threads in the process to
    /// generate load and to verify the IO.
    pub thread_count: usize,

    /// Specifies how a `target` is opened, what type of IO functions are called
    /// and how completions of those IOs will be delivered. For example fdio
    /// might call posix-like pwrite, pread, etc. but blob target may have
    /// completely different restrictions on issuing IOs.
    pub target_type: AvailableTargets,

    /// These are the set of operations for which generator will generate
    /// io packets. These operation performance is what user is interested
    /// in.
    pub operations: TargetOps,

    /// When true, the `target` access (read/write) are sequential with respect
    /// to offsets within the `target`.
    pub sequential: bool,

    /// Parameters passed to odu gets written to `output_config_file`.
    pub output_config_file: String,

    /// A `target` can be a path in filesystem, a hash in blobfs, a path in
    /// device tree, or a named pipe. These are pre-existing targets that have
    /// a non-zero length. IOs are performed only on these targets. All threads
    /// get exclusive access to certain parts of the `target`.
    pub target: String,
}

const KIB: u64 = 1024;
const MIB: u64 = KIB * KIB;

// TODO(auradkar): Some of the default values/ranges are intentionally set low
//                 so that the tool runs for shorter duration.
//                 And some of the defaults and ranges have arbitrarily values.
//                 We need to come up with better ones.
const QUEUE_DEPTH_RANGE: RangeInclusive<usize> = 1..=256;
const QUEUE_DEPTH_DEFAULT: usize = 40;

const BLOCK_SIZE_RANGE: RangeInclusive<u64> = 1..=MIB;
const BLOCK_SIZE_DEFAULT: u64 = 8 * KIB;

const MAX_IO_SIZE_RANGE: RangeInclusive<u64> = 1..=MIB;
const MAX_IO_SIZE_DEFAULT: u64 = MIB;

const ALIGN_DEFAULT: bool = true;

const MAX_IO_COUNT_RANGE: RangeInclusive<u64> = 1..=2_000_000;
const MAX_IO_COUNT_DEFAULT: u64 = 1000;

const TARGET_SIZE_DEFAULT: u64 = 20 * MIB;

const THREAD_COUNT_RANGE: RangeInclusive<usize> = 1..=16;
const THREAD_COUNT_DEFAULT: usize = 3;

const TARGET_OPERATIONS_DEFAULT: &str = "write";

const TARGET_TYPE_DEFAULT: AvailableTargets = AvailableTargets::FileTarget;

const SEQUENTIAL_DEFAULT: bool = true;

const OUTPUT_CONFIG_FILE_DEFAULT: &str = "/tmp/output.config";

fn to_string_min_max<T: std::fmt::Debug>(val: RangeInclusive<T>) -> String {
    format!("Min:{:?} Max:{:?}", val.start(), val.end())
}

fn validate_range<T: std::str::FromStr + std::cmp::PartialOrd>(
    key: &str,
    val: String,
    range: RangeInclusive<T>,
) -> Result<(), String> {
    let arg =
        val.parse::<T>().map_err(|_| format!("{} expects a number. Found \"{}\"", key, val))?;

    if !range.contains(&arg) {
        return Err(format!(" {} value {} out of range.", key, val));
    }

    Ok(())
}

fn queue_depth_validator(val: String) -> Result<(), String> {
    validate_range("queue_depth", val, QUEUE_DEPTH_RANGE)
}

fn block_size_validator(val: String) -> Result<(), String> {
    validate_range("block_size", val, BLOCK_SIZE_RANGE)
}

fn max_io_size_validator(val: String) -> Result<(), String> {
    validate_range("max_io_size", val, MAX_IO_SIZE_RANGE)
}

fn max_io_count_validator(val: String) -> Result<(), String> {
    validate_range("max_io_count", val, MAX_IO_COUNT_RANGE)
}

fn thread_count_validator(val: String) -> Result<(), String> {
    validate_range("thread_count", val, THREAD_COUNT_RANGE)
}

fn target_operations_validator(
    target_type: AvailableTargets,
    operations: &Vec<&str>,
) -> Result<TargetOps, Error> {
    // Get the operations allowed by the target and see if the operations requested
    // is subset of the operations allowed.
    let allowed_ops = allowed_ops(target_type);

    let mut ops = TargetOps { write: false, open: false };
    for value in operations {
        if !allowed_ops.enabled(value) {
            error!(
                "{:?} is not allowed for target: {}",
                value,
                AvailableTargets::value_to_friendly_name(target_type)
            );
            error!(
                "For target: {}, supported operations are {:?}",
                AvailableTargets::value_to_friendly_name(target_type),
                allowed_ops.enabled_operation_names()
            );
            return Err(Error::OperationNotSupported);
        } else {
            ops.enable(value, true).unwrap();
        }
    }

    return Ok(ops);
}

pub fn parse() -> Result<ParseArgs, Error> {
    let queue_depth_default_str = &format!("{}", QUEUE_DEPTH_DEFAULT);
    let block_size_default_str = &format!("{}", BLOCK_SIZE_DEFAULT);
    let max_io_size_default_str = &format!("{}", MAX_IO_SIZE_DEFAULT);
    let align_default_str = &format!("{}", ALIGN_DEFAULT);
    let max_io_count_default_str = &format!("{}", MAX_IO_COUNT_DEFAULT);
    let target_size_default_str = &format!("{}", TARGET_SIZE_DEFAULT);
    let thread_count_default_str = &format!("{}", THREAD_COUNT_DEFAULT);
    let target_operations_default_str = &format!("{}", TARGET_OPERATIONS_DEFAULT);
    let target_type_default_str =
        &format!("{}", AvailableTargets::value_to_friendly_name(TARGET_TYPE_DEFAULT));
    let sequential_default_str = &format!("{}", SEQUENTIAL_DEFAULT);
    let output_config_file_default_str = &format!("{}", OUTPUT_CONFIG_FILE_DEFAULT);

    let matches = App::new("odu")
        // TODO: We cannot get package version through `CARGO_PKG_VERSION`.
        //       Find out a way.
        .version("0.1.0")
        .about("IO benchmarking library and utility")
        .arg(
            Arg::with_name("queue_depth")
                .short("q")
                .long("queue_depth")
                .value_name(&to_string_min_max(QUEUE_DEPTH_RANGE))
                .default_value(queue_depth_default_str)
                .validator(queue_depth_validator)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("block_size")
                .short("b")
                .long("block_size")
                .value_name(&to_string_min_max(BLOCK_SIZE_RANGE))
                .default_value(&block_size_default_str)
                .validator(block_size_validator)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("max_io_size")
                .short("i")
                .long("max_io_size")
                .value_name(&to_string_min_max(MAX_IO_SIZE_RANGE))
                .default_value(&max_io_size_default_str)
                .validator(max_io_size_validator)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("align")
                .short("a")
                .long("align")
                .possible_values(&["true", "false"])
                .default_value(&align_default_str)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("max_io_count")
                .short("c")
                .long("max_io_count")
                .value_name(&to_string_min_max(MAX_IO_COUNT_RANGE))
                .default_value(&max_io_count_default_str)
                .validator(max_io_count_validator)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("target_length")
                .short("l")
                .long("target_length")
                .value_name(&to_string_min_max(0..=std::u64::MAX))
                .default_value(&target_size_default_str)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("thread_count")
                .short("d")
                .long("thread_count")
                .value_name(&to_string_min_max(THREAD_COUNT_RANGE))
                .default_value(&thread_count_default_str)
                .validator(thread_count_validator)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("operations")
                .short("n")
                .long("operations")
                .possible_values(&TargetOps::friendly_names())
                .default_value(&target_operations_default_str)
                .help(
                    "Types of operations to generate load for. Not all operations \
                     are allowed for all targets",
                )
                .takes_value(true)
                .use_delimiter(true)
                .multiple(true),
        )
        .arg(
            Arg::with_name("target_type")
                .short("p")
                .long("target_type")
                .possible_values(&AvailableTargets::friendly_names()[..])
                .default_value(&target_type_default_str)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("sequential")
                .short("s")
                .long("sequential")
                .possible_values(&["true", "false"])
                .default_value(&sequential_default_str)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("output_config_file")
                .short("o")
                .long("output_config_file")
                .value_name("FILE")
                .default_value(&output_config_file_default_str)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("target")
                .short("t")
                .long("target")
                .value_name("FILE")
                .required(true)
                .help("Maximum number of outstanding IOs per thread.")
                .takes_value(true),
        )
        .get_matches();

    let mut args = ParseArgs {
        queue_depth: matches.value_of("queue_depth").unwrap().parse::<usize>().unwrap(),
        block_size: matches.value_of("block_size").unwrap().parse::<u64>().unwrap(),
        max_io_size: matches.value_of("max_io_size").unwrap().parse::<u64>().unwrap(),
        align: matches.value_of("align").unwrap().parse::<bool>().unwrap(),
        max_io_count: matches.value_of("max_io_count").unwrap().parse::<u64>().unwrap(),
        target_length: matches.value_of("target_length").unwrap().parse::<u64>().unwrap(),
        thread_count: matches.value_of("thread_count").unwrap().parse::<usize>().unwrap(),
        target_type: AvailableTargets::friendly_name_to_value(
            matches.value_of("target_type").unwrap(),
        )
        .unwrap(),
        operations: Default::default(),
        sequential: matches.value_of("sequential").unwrap().parse::<bool>().unwrap(),
        output_config_file: matches.value_of("output_config_file").unwrap().to_string(),
        target: matches.value_of("target").unwrap().to_string(),
    };

    args.operations = target_operations_validator(
        args.target_type,
        &matches.values_of("operations").unwrap().collect::<Vec<_>>(),
    )?;

    Ok(args)
}

#[cfg(test)]
mod tests {
    use {crate::args, crate::common_operations::allowed_ops, crate::target::AvailableTargets};

    #[test]
    fn queue_depth_validator_test_default() {
        assert!(args::queue_depth_validator(args::QUEUE_DEPTH_DEFAULT.to_string()).is_ok());
    }

    #[test]
    fn queue_depth_validator_test_out_of_range() {
        assert!(
            args::queue_depth_validator((args::QUEUE_DEPTH_RANGE.end() + 1).to_string()).is_err()
        );
    }

    #[test]
    fn block_size_validator_test_default() {
        assert!(args::block_size_validator(args::BLOCK_SIZE_DEFAULT.to_string()).is_ok());
    }

    #[test]
    fn block_size_validator_test_out_of_range() {
        assert!(args::block_size_validator((args::BLOCK_SIZE_RANGE.end() + 1).to_string()).is_err());
    }

    #[test]
    fn max_io_size_validator_test_default() {
        assert!(args::max_io_size_validator(args::MAX_IO_SIZE_DEFAULT.to_string()).is_ok());
    }

    #[test]
    fn max_io_size_validator_test_out_of_range() {
        assert!(
            args::max_io_size_validator((args::MAX_IO_SIZE_RANGE.end() + 1).to_string()).is_err()
        );
    }

    #[test]
    fn max_io_count_validator_test_default() {
        assert!(args::max_io_count_validator(args::MAX_IO_COUNT_DEFAULT.to_string()).is_ok());
    }

    #[test]
    fn max_io_count_validator_test_out_of_range() {
        assert!(
            args::max_io_count_validator((args::MAX_IO_COUNT_RANGE.end() + 1).to_string()).is_err()
        );
    }

    #[test]
    fn thread_count_validator_test_default() {
        assert!(args::thread_count_validator(args::THREAD_COUNT_DEFAULT.to_string()).is_ok());
    }

    #[test]
    fn thread_count_validator_test_out_of_range() {
        assert!(
            args::thread_count_validator((args::THREAD_COUNT_RANGE.end() + 1).to_string()).is_err()
        );
    }

    #[test]
    fn target_operations_validator_test_valid_inputs() {
        let allowed_ops = allowed_ops(AvailableTargets::FileTarget);

        // We know that "write" is allowed for files. Input "write" to the
        // function and expect success.
        assert_eq!(allowed_ops.enabled("write"), true);

        assert!(
            args::target_operations_validator(AvailableTargets::FileTarget, &vec!["write"]).is_ok()
        );
    }

    #[test]
    fn target_operations_validator_test_invalid_input_nonexistant_operation() {
        let ret = args::target_operations_validator(AvailableTargets::FileTarget, &vec!["hello"]);
        assert!(ret.is_err());
        assert_eq!(ret.err(), Some(args::Error::OperationNotSupported));
    }

    #[test]
    fn target_operations_validator_test_invalid_input_disallowed_operation() {
        let allowed_ops = allowed_ops(AvailableTargets::FileTarget);

        // We know that "open" is not *yet* allowed for files. Input "open" to the
        // function and expect success.
        assert_eq!(allowed_ops.enabled("open"), false);

        assert!(
            args::target_operations_validator(AvailableTargets::FileTarget, &vec!["open"]).is_err()
        );
    }
}
