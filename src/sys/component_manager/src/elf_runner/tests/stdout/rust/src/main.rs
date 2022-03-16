fn main() {
    println!("Hello Stdout!");
    eprintln!("Hello Stderr!");

    // TODO(https://fxbug.dev/95602) delete this when clean shutdown works
    std::thread::sleep(std::time::Duration::MAX);
}
