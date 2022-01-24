# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import unittest
import subprocess
import sys
from subprocess import CalledProcessError
from pathlib import Path

# the host_cpu specific dir is passed as an arg
HOST = Path(sys.argv.pop())
FAKE_ROOT = HOST / "gen/build/rust/tests"
FAKE_OUT = FAKE_ROOT / "out/default"
TEST_DIR = Path("gen/build/rust/tests")


def run_clippy(*args):
    return subprocess.check_output(
        [
            sys.executable,
            FAKE_ROOT / "tools/devshell/contrib/lib/rust/clippy.py",
            "--no-build",
            f"--out-dir={FAKE_OUT}",
            f"--fuchsia-dir={FAKE_ROOT}",
        ]
        + list(args),
        text=True,
    )


def read_lints(raw):
    return [json.loads(line) for line in raw.splitlines()]


def extract_codes(lints):
    return sorted([l["code"]["code"] for l in lints])


class TestClippy(unittest.TestCase):
    def test_expected(self):
        lints = read_lints(run_clippy("//build/rust/tests:a", "--raw"))
        codes = extract_codes(lints)
        self.assertEqual(codes, ["clippy::needless_return"])

    def test_unit_test(self):
        lints = read_lints(run_clippy("//build/rust/tests:a_test", "--raw"))
        codes = extract_codes(lints)
        self.assertEqual(codes, ["clippy::approx_constant", "clippy::needless_return"])

    def test_file_filtering(self):
        lints = read_lints(
            run_clippy("-f", FAKE_ROOT / "build/rust/tests/a/main.rs", "--raw")
        )
        codes = extract_codes(lints)
        self.assertEqual(codes, ["clippy::approx_constant"])

        lints = read_lints(
            run_clippy("-f", FAKE_ROOT / "build/rust/tests/a/other.rs", "--raw")
        )
        codes = extract_codes(lints)
        self.assertEqual(codes, ["clippy::needless_return"])

    def test_dedup_lints(self):
        lints = read_lints(
            run_clippy(
                "-f",
                FAKE_ROOT / "build/rust/tests/a/main.rs",
                FAKE_ROOT / "build/rust/tests/a/other.rs",
                "--raw",
            )
        )
        codes = extract_codes(lints)
        self.assertEqual(codes, ["clippy::approx_constant", "clippy::needless_return"])

    def test_depfiles(self):
        with open(FAKE_OUT / TEST_DIR / "a.clippy.deps") as f:
            self.assertEqual(
                f.read().splitlines(), ["--extern=b=obj/build/rust/tests/libb.rlib"]
            )
        with open(FAKE_OUT / TEST_DIR / "b.clippy.deps") as f:
            self.assertEqual(
                f.read().splitlines(), ["--extern=c=obj/build/rust/tests/libc.rlib"]
            )
        with open(FAKE_OUT / TEST_DIR / "a.clippy.transdeps") as f:
            self.assertIn(
                "-Ldependency=obj/build/rust/tests",
                f.read().splitlines(),
            )

    def test_file_mapping(self):
        output = run_clippy(
            "--get-outputs", "-f", FAKE_ROOT / "build/rust/tests/b/lib.rs"
        )
        self.assertEqual(
            set(output.splitlines()),
            {str(TEST_DIR / "b.clippy")},
        )

        output = run_clippy(
            "--get-outputs", "-f", FAKE_ROOT / "build/rust/tests/a/main.rs"
        )
        self.assertEqual(
            set(output.splitlines()),
            {str(TEST_DIR / "a.clippy"), str(TEST_DIR / "a_test.clippy")},
        )

    def test_dedup_files(self):
        outputs = run_clippy(
            "--get-outputs",
            "-f",
            FAKE_ROOT / "build/rust/tests/a/main.rs",
            FAKE_ROOT / "build/rust/tests/a/other.rs",
        ).splitlines()
        # main.rs has normal and "test" clippy targets and other.rs has the same
        # normal clippy target, so there should be 2 unique outputs
        self.assertEqual(
            sorted(outputs),
            ["gen/build/rust/tests/a.clippy", "gen/build/rust/tests/a_test.clippy"],
        )

    def test_not_found(self):
        with self.assertRaises(CalledProcessError):
            run_clippy("-f", "NOT_A_RUST_FILE")

        with self.assertRaises(CalledProcessError):
            run_clippy("NOT_A_RUST_TARGET")

    def test_host_toolchain(self):
        lints = read_lints(
            run_clippy(f"//build/rust/tests:d(//build/toolchain:{HOST})", "--raw")
        )
        codes = extract_codes(lints)
        self.assertEqual(codes, ["clippy::needless_return"])

        lints = read_lints(
            run_clippy("-f", FAKE_ROOT / "build/rust/tests/d/lib.rs", "--raw")
        )
        codes = extract_codes(lints)
        self.assertEqual(codes, ["clippy::needless_return"])

        with self.assertRaises(CalledProcessError):
            run_clippy("//build/rust/tests:d")


if __name__ == "__main__":
    unittest.main()
