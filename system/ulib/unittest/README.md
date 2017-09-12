# ulib/unittest

This directory contains a harness for writing tests used by system/utest.

N.B. This library cannot use fdio since system/utest/core uses it
and system/utest/core cannot use fdio. See system/utest/core/README.md.
