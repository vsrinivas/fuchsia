# Instruction to perform a backport from upstream

- Checkout upstream branch at https://fuchsia.googlesource.com/mundane/#

- Change copyright header to Fuchsia copyright

- Change boringssl:ffi module to boringssl_sys

- Run 'fx format-code'

# Difference between upstream and mundane garnet module

Please refer to 0001-Mundane-backport.patch

