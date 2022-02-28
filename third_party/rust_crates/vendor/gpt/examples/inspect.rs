use gpt;

use simplelog::{Config, LevelFilter, SimpleLogger};
use std::io;

fn main() {
    // Setup logging
    let _ = SimpleLogger::init(LevelFilter::Warn, Config::default());

    // Inspect disk image, handling errors.
    if let Err(e) = run() {
        eprintln!("Failed to inspect image: {}", e);
        std::process::exit(1)
    }
}

fn run() -> io::Result<()> {
    // First parameter is target disk image (optional, default: fixtures sample)
    let sample = "tests/fixtures/gpt-linux-disk-01.img".to_string();
    let input = std::env::args().nth(1).unwrap_or(sample);

    // Open disk image.
    let cfg = gpt::GptConfig::new().writable(false);
    let disk = cfg.open(input)?;

    // Print GPT layout.
    println!("Disk (primary) header: {:#?}", disk.primary_header());
    println!("Partition layout: {:#?}", disk.partitions());

    Ok(())
}
