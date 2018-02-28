Rust DDK Toy Installation Instructions
======================================
In order to have the system load this driver at boot, perform the following steps:

1. Install Fargo https://fuchsia.googlesource.com/fargo/+/master/README.md
2. Ensure you're building a debug build of Fuchsia on x64.
3. Run 'fargo load-driver' to install onto your target machine.
