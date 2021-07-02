//! # **Humansize**
//!
//! Humansize lets you easily represent file sizes in a human-friendly format.
//! You can specify your own formatting style, pick among the three defaults provided
//! by the library:
//!
//! * Decimal (Multiples of 1000, `KB` units)
//! * Binary (Multiples of 1024, `KiB` units)
//! * Conventional (Multiples of 1024, `KB` units)
//!
//! ## How to use it
//!
//! Simply import the `FileSize` trait and the options module and call the
//! file_size method on any positive integer, using one of the three standards
//! provided by the options module.
//!
//! ```rust
//! extern crate humansize;
//! use humansize::{FileSize, file_size_opts as options};
//!
//! fn main() {
//! 	let size = 1000;
//! 	println!("Size is {}", size.file_size(options::DECIMAL).unwrap());
//!
//! 	println!("Size is {}", size.file_size(options::BINARY).unwrap());
//!
//! 	println!("Size is {}", size.file_size(options::CONVENTIONAL).unwrap());
//! }
//! ```
//!
//! If you wish to customize the way sizes are displayed, you may create your own custom `FileSizeOpts` struct
//! and pass that to the method. See the `custom_options.rs` file in the example folder.

static SCALE_DECIMAL: [&str; 9] = ["B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"];
static SCALE_DECIMAL_LONG: [&str; 9] = [
    "Bytes",
    "Kilobytes",
    "Megabytes",
    "Gigabytes",
    "Terabytes",
    "Petabytes",
    "Exabytes",
    "Zettabytes",
    "Yottabytes",
];

static SCALE_BINARY: [&str; 9] = ["B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"];
static SCALE_BINARY_LONG: [&str; 9] = [
    "Bytes",
    "Kibibytes",
    "Mebibytes",
    "Gibibytes",
    "Tebibytes",
    "Pebibytes",
    "Exbibytes",
    "Zebibytes",
    "Yobibytes",
];

pub mod file_size_opts {
    //! Describes the struct that holds the options needed by the `file_size` method.
    //! The three most common formats are provided as constants to be used easily

    #[derive(Debug, PartialEq, Copy, Clone)]
    /// Holds the standard to use when displying the size.
    pub enum Kilo {
        /// The decimal scale and units
        Decimal,
        /// The binary scale and units
        Binary,
    }

    #[derive(Debug, Copy, Clone)]
    /// Forces a certain representation of the resulting file size.
    pub enum FixedAt {
        Byte,
        Kilo,
        Mega,
        Giga,
        Tera,
        Peta,
        Exa,
        Zetta,
        Yotta,
        No,
    }

    /// Holds the options for the `file_size` method.
    #[derive(Debug)]
    pub struct FileSizeOpts {
        /// The scale (binary/decimal) to divide against.
        pub divider: Kilo,
        /// The unit set to display.
        pub units: Kilo,
        /// The amount of decimal places to display if the decimal part is non-zero.
        pub decimal_places: usize,
        /// The amount of zeroes to display if the decimal part is zero.
        pub decimal_zeroes: usize,
        /// Whether to force a certain representation and if so, which one.
        pub fixed_at: FixedAt,
        /// Whether to use the full suffix or its abbreveation.
        pub long_units: bool,
        /// Whether to place a space between value and units.
        pub space: bool,
        /// An optional suffix which will be appended after the unit.
        pub suffix: &'static str,
        /// Whether to allow negative numbers as input. If `False`, negative values will return an error.
        pub allow_negative: bool,
    }

    impl AsRef<FileSizeOpts> for FileSizeOpts {
        fn as_ref(&self) -> &FileSizeOpts {
            self
        }
    }

    /// Options to display sizes in the binary format.
    pub const BINARY: FileSizeOpts = FileSizeOpts {
        divider: Kilo::Binary,
        units: Kilo::Binary,
        decimal_places: 2,
        decimal_zeroes: 0,
        fixed_at: FixedAt::No,
        long_units: false,
        space: true,
        suffix: "",
        allow_negative: false,
    };

    /// Options to display sizes in the decimal format.
    pub const DECIMAL: FileSizeOpts = FileSizeOpts {
        divider: Kilo::Decimal,
        units: Kilo::Decimal,
        decimal_places: 2,
        decimal_zeroes: 0,
        fixed_at: FixedAt::No,
        long_units: false,
        space: true,
        suffix: "",
        allow_negative: false,
    };

    /// Options to display sizes in the "conventional" format.
    /// This 1024 as the value of the `Kilo`, but displays decimal-style units (`KB`, not `KiB`).
    pub const CONVENTIONAL: FileSizeOpts = FileSizeOpts {
        divider: Kilo::Binary,
        units: Kilo::Decimal,
        decimal_places: 2,
        decimal_zeroes: 0,
        fixed_at: FixedAt::No,
        long_units: false,
        space: true,
        suffix: "",
        allow_negative: false,
    };
}
/// The trait for the `file_size`method
pub trait FileSize {
    /// Formats self according to the parameters in `opts`. `opts` can either be one of the
    /// three defaults providedby the `file_size_opts` module, or be custom-defined according
    /// to your needs
    ///
    /// # Errors
    /// Will fail by default if called on a negative number. Override this behavior by setting
    /// `allow_negative` to `True` in a custom options struct.
    ///
    /// # Examples
    /// ```rust
    /// use humansize::{FileSize, file_size_opts as options};
    ///
    /// let size = 5128;
    /// println!("Size is {}", size.file_size(options::DECIMAL).unwrap());
    /// ```
    ///
    fn file_size<T: AsRef<FileSizeOpts>>(&self, opts: T) -> Result<String, String>;
}

fn f64_eq(left: f64, right: f64) -> bool {
    left == right || (left - right).abs() <= std::f64::EPSILON
}

use self::file_size_opts::*;

macro_rules! impl_file_size_u {
    (for $($t:ty)*) => ($(
        impl FileSize for $t {
            fn file_size<T: AsRef<FileSizeOpts>>(&self, _opts: T) -> Result<String, String> {
                let opts = _opts.as_ref();
                let divider = match opts.divider {
                    Kilo::Decimal => 1000.0,
                    Kilo::Binary => 1024.0
                };

                let mut size: f64 = *self as f64;
                let mut scale_idx = 0;

                match opts.fixed_at {
                    FixedAt::No => {
                        while size >= divider {
                            size /= divider;
                            scale_idx += 1;
                        }
                    }
                    val => {
                        while scale_idx != val as usize {
                            size /= divider;
                            scale_idx += 1;
                        }
                    }
                }

                let mut scale = match (opts.units, opts.long_units) {
                    (Kilo::Decimal, false) => SCALE_DECIMAL[scale_idx],
                    (Kilo::Decimal, true) => SCALE_DECIMAL_LONG[scale_idx],
                    (Kilo::Binary, false) => SCALE_BINARY[scale_idx],
                    (Kilo::Binary, true) => SCALE_BINARY_LONG[scale_idx]
                };

                // Remove "s" from the scale if the size is 1.x
                if opts.long_units && f64_eq(size.trunc(), 1.0) { scale = &scale[0 .. scale.len()-1]; }

                let places = if f64_eq(size.fract(), 0.0) {
                    opts.decimal_zeroes
                } else {
                    opts.decimal_places
                };

                let space = if opts.space {" "} else {""};

                Ok(format!("{:.*}{}{}{}", places, size, space, scale, opts.suffix))
            }
        }
    )*)
}

macro_rules! impl_file_size_i {
    (for $($t:ty)*) => ($(
        impl FileSize for $t {
            fn file_size<T: AsRef<FileSizeOpts>>(&self, _opts: T) -> Result<String, String> {
                let opts = _opts.as_ref();
                if *self < 0 && !opts.allow_negative {
                    return Err("Tried calling file_size on a negative value".to_owned());
                } else {
                    let sign = if *self < 0 {
                        "-"
                    } else {
                        ""
                    };

                    Ok(format!("{}{}", sign, (self.abs() as u64).file_size(opts)?))
                }

            }
        }
    )*)
}

impl_file_size_u!(for usize u8 u16 u32 u64);
impl_file_size_i!(for isize i8 i16 i32 i64);

#[test]
fn test_sizes() {
    assert_eq!(0.file_size(BINARY).unwrap(), "0 B");
    assert_eq!(999.file_size(BINARY).unwrap(), "999 B");
    assert_eq!(1000.file_size(BINARY).unwrap(), "1000 B");
    assert_eq!(1000.file_size(DECIMAL).unwrap(), "1 KB");
    assert_eq!(1023.file_size(BINARY).unwrap(), "1023 B");
    assert_eq!(1023.file_size(DECIMAL).unwrap(), "1.02 KB");
    assert_eq!(1024.file_size(BINARY).unwrap(), "1 KiB");
    assert_eq!(1024.file_size(CONVENTIONAL).unwrap(), "1 KB");

    let semi_custom_options = file_size_opts::FileSizeOpts {
        space: false,
        ..file_size_opts::DECIMAL
    };
    assert_eq!(1000.file_size(semi_custom_options).unwrap(), "1KB");

    let semi_custom_options2 = file_size_opts::FileSizeOpts {
        suffix: "/s",
        ..file_size_opts::BINARY
    };
    assert_eq!(999.file_size(semi_custom_options2).unwrap(), "999 B/s");

    let semi_custom_options3 = file_size_opts::FileSizeOpts {
        suffix: "/day",
        space: false,
        ..file_size_opts::DECIMAL
    };
    assert_eq!(1000.file_size(semi_custom_options3).unwrap(), "1KB/day");

    let semi_custom_options4 = file_size_opts::FileSizeOpts {
        fixed_at: file_size_opts::FixedAt::Byte,
        ..file_size_opts::BINARY
    };
    assert_eq!(2048.file_size(semi_custom_options4).unwrap(), "2048 B");

    let semi_custom_options5 = file_size_opts::FileSizeOpts {
        fixed_at: file_size_opts::FixedAt::Kilo,
        ..file_size_opts::BINARY
    };
    assert_eq!(
        16584975.file_size(semi_custom_options5).unwrap(),
        "16196.26 KiB"
    );

    let semi_custom_options6 = file_size_opts::FileSizeOpts {
        fixed_at: file_size_opts::FixedAt::Tera,
        decimal_places: 10,
        ..file_size_opts::BINARY
    };
    assert_eq!(
        15284975.file_size(semi_custom_options6).unwrap(),
        "0.0000139016 TiB"
    );

    let semi_custom_options7 = file_size_opts::FileSizeOpts {
        allow_negative: true,
        ..file_size_opts::DECIMAL
    };
    assert_eq!(
        (-5500).file_size(&semi_custom_options7).unwrap(),
        "-5.50 KB"
    );
    assert_eq!((5500).file_size(&semi_custom_options7).unwrap(), "5.50 KB");
}
