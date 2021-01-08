# CPUID value corpus

This directory holds a corpus of CPUID values from a variety of
microprocessors. Each file gives the set of CPUID values for a particular one
and is meant to be #include'd with the DEFINE_CPUID_VALUES macro defined,
wherein in each line is of the form
```
DEF_CPUID_VALUES(leaf, subleaf, EAX, EBX, ECX, EDX)
```

Usage would look like
```
#define DEFINE_CPUID_VALUES(leaf, subleaf, eax, ebx, ecx, edx) ...
#include "data/cpuid/my-microprocessor.inc"
#undef DEFINE_CPUID_VALUES
```

For the convenience of extending the corpus without writing the values out by
hand, one can use [converter.py](converter.py) to convert the raw output of the
[`cpuid` tool](https://linux.die.net/man/1/cpuid) to our format. (The tool is
only available on Linux.) The python script expects the raw cpuid output on
stdin and writes the output to stdout.

Example usage:
```
cpuid -r1 | converter.py > cpuid.inc
```
(`cpuid -r` may also be piped into the script.)

Any addition to the corpus is expected to be given an associated value in
[`arch::testing::X86Microprocessor`](/zircon/kernel/lib/arch/testing/fake-cpuid.h)
and a set of expectation tests extending those written in
[cpuid-corpus-tests.cc](/zircon/kernel/lib/arch/test/cpuid-corpus-tests.cc).