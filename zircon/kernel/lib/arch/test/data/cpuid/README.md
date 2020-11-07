# CPUID value corpus

This directory holds a corpus of CPUID values from a variety of processors. The
format is a simple, self-documenting JSON schema.

For the convenience of extending the corpus without writing the JSON by hand,
one can use [converter.py](converter.py) to convert the raw output of the
[`cpuid` tool](https://linux.die.net/man/1/cpuid) to our JSON format. (The tool
is only available on Linux.) The python script expects the raw cpuid output on
stdin and writes the JSON to stdout.

Example usage:
```
cpuid -r1 | converter.py > cpuid.json
```
(`cpuid -r` may also be piped into the script.)

An extensive suite of expectation tests against the corpus is written in
[cpuid-corpus-tests.cc](/zircon/kernel/lib/arch/test/cpuid-corpus-tests.cc).
Any addition to the corpus is expected to be referenced in that file - with a
similar set of tests - as well as in the source list in [BUILD.gn](BUILD.gn).