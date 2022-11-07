extern crate humansize;
use humansize::{file_size_opts as opts, FileSize};

fn main() {
    // Declare a fully custom option struct
    let custom_options = opts::FileSizeOpts {
        divider: opts::Kilo::Binary,
        units: opts::Kilo::Decimal,
        decimal_places: 3,
        decimal_zeroes: 1,
        fixed_at: opts::FixedAt::No,
        long_units: true,
        space: false,
        suffix: "",
        allow_negative: true,
    };
    // Then use it
    println!("{}", 3024.file_size(custom_options).unwrap());

    // Or use only some custom parameters and adopt the rest from an existing config
    let semi_custom_options = opts::FileSizeOpts {
        decimal_zeroes: 3,
        ..opts::DECIMAL
    };

    println!("{}", 1000.file_size(semi_custom_options).unwrap());
}
