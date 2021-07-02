extern crate humansize;
//Import the trait and the options module
use humansize::{file_size_opts, FileSize};

fn main() {
    // Call the file_size method on any non-negative integer with the option set you require

    println!("{}", 5456.file_size(file_size_opts::BINARY).unwrap());
    println!("{}", 1024.file_size(file_size_opts::BINARY).unwrap());
    println!("{}", 1000.file_size(file_size_opts::DECIMAL).unwrap());
    println!(
        "{}",
        1023_654_123_654u64
            .file_size(file_size_opts::DECIMAL)
            .unwrap()
    );
    println!(
        "{}",
        123456789.file_size(file_size_opts::CONVENTIONAL).unwrap()
    );
}
