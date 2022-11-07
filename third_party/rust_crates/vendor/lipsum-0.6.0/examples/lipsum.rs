extern crate lipsum;

use std::env;

fn main() {
    // Generate lorem ipsum text with Title Case.
    let title = lipsum::lipsum_title();
    // Print underlined title and lorem ipsum text.
    println!("{}\n{}\n", title, str::repeat("=", title.len()));

    // First command line argument or "" if not supplied.
    let arg = env::args().nth(1).unwrap_or_default();
    // Number of words to generate.
    let n = arg.parse().unwrap_or(25);
    // Print n words of lorem ipsum text.
    println!("{}", lipsum::lipsum(n));
}
