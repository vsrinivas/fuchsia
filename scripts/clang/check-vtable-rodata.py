#!usr/bin/env python3
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Script for checking that all vtables in an elf file are in rodata. This script
provides different "modes" for quick checking of different files/project
layouts.

Usage (append -h for details):

  # Check an individual file.
  $ ./scripts/clang/check-vtable-rodata.py file [filename]

  # Check all vtables in binaries provided by the fuchsia toolchain
  $ ./scripts/clang/check-vtable-rodata.py toolchain [toolchain_dir]

  # Check all vtables in binaries provided by an llvm multilib directory.
  $ ./scripts/clang/check-vtable-rodata.py multilib [multilib_dir]

  # Check all vtables in binaries provided in a fuchsia build directory.
  $ ./scripts/clang/check-vtable-rodata.py fuchsia [fuchsia_dir]

  # Exclude checking specific vtable symbols
  $ ./scripts/clang/check-vtable-rodata.py --exclude 'vtable for ...' file [filename]
"""

import argparse
import os.path
import platform
import subprocess
from pathlib import Path


class RawDescriptionDefaultArgsHelpFormatter(argparse.ArgumentDefaultsHelpFormatter,
                                             argparse.RawDescriptionHelpFormatter):
  pass


if platform.system() == "Darwin":
  target = "mac-x64"
elif platform.system() == "Linux":
  if platform.machine() == "x86_64":
    target = "linux-x64"
  elif platform.machine() == "aarch64":
    target = "linux-arm64"
  else:
    raise Exception("Clang toolchain not provided for platform machine " + platform.machine())

# Point to the readelf provided in the clang toolchain.
DEFAULT_READELF = Path(__file__).parent.parent.parent / "prebuilt" / "third_party" / "clang" / target / "bin" / "llvm-readelf"


def is_elf_file(filename):
  """Check if a file is an ELF file."""
  with open(filename, "rb") as f:
    return f.read(4) == b"\x7fELF"


def is_elf_dso(filename, readelf):
  """Check if a file is an ELF shared library."""
  try:
    result = subprocess.check_output([str(readelf), "-hW", filename], text=True)
  except subprocess.CalledProcessError:
    return False
  for line in result.split("\n"):
    parts = line.split()
    if not parts:
      continue

    if parts[0] == "Type:":
      type = parts[1]
      if type == "DYN":
        return True

  # Note that the readelf could fail here if the file could not have been parsed
  # by readelf in the first place.
  return False


def find_vtable_symbols(filename, readelf):
  """Collect a map of all symbols and their sections in an elf file."""
  symbols = {}

  # Lines will look like:
  #
  #   1: 0000000000000000     0 FUNC    GLOBAL DEFAULT   UND std::terminate()
  #
  # Which will have 8 parts unless the Name is not provided
  numparts = 8

  result = subprocess.check_output(
      [str(readelf), "-W", "--demangle", "--symbols", filename], text=True)
  for line in result.split("\n"):
    parts = line.split(maxsplit=numparts - 1)
    if len(parts) != numparts:
      continue

    symbol = parts[-1]
    section = parts[-2]
    if not section.isdigit():
      continue

    if not symbol.startswith("vtable for "):
      continue

    symbols[symbol.strip()] = int(section)

  return symbols


def parse_section(line):
  """Return a tuple of the section number and name in a line from
  `readelf -W --sections`. If one couldn't be parsed from here, return None.
  """
  numstart = None
  numend = None
  for i, c in enumerate(line):
    if numstart is None:
      if c == "[":
        numstart = i + 1
    elif numend is None:
      # Start parsing the number
      if c == "]":
        numend = i
        break

  if (numstart is None) or (numend is None):
    return None

  try:
    section_num = int(line[numstart:numend].strip())
  except ValueError:
    return None

  name = line[numend + 1:].split(maxsplit=1)[0]
  if name == "NULL":
    assert section_num == 0, "Expected the null section to be 0"

  return section_num, name


def find_sections(filename, readelf):
  """Collect a map of all sections."""
  sections = []

  result = subprocess.check_output(
      [str(readelf), "-W", "--sections", filename], text=True)
  for line in result.split("\n"):
    result = parse_section(line)
    if result is None:
      continue

    section_num, name = result
    assert len(sections) == section_num, f"Expected '{name}' to correspond to section number {len(sections)} but is instead section number {section_num}"
    sections.append(name)

  assert sections, f"Found no sections for '{filename}'"
  return sections


def check_vtables_in_dso(filename, readelf, excludes):
  assert isinstance(excludes, set)
  print("Checking", filename)

  symbols = find_vtable_symbols(filename, readelf)
  print("Found {} vtables".format(len(symbols)))

  sections = find_sections(filename, readelf)
  print("Found {} sections".format(len(sections)))

  # Find the rodata section.
  for i, section in enumerate(sections):
    if section == ".rodata":
      rodata = i
      break
  else:
    print("Could not find rodata in ", sections)
    return

  bad_vtables = []
  for vtable, section in symbols.items():
    if section != rodata and vtable not in excludes:
      bad_vtables.append(vtable)

  assert not bad_vtables, f"Found vtables that aren't in rodata:\n{str(bad_vtables)}"
  print("All vtables are in rodata :)")


def check_vtables_in_file(filename, readelf, excludes):
  if is_elf_file(filename):
    # FIXME: We should also be checking executables.
    check_vtables_in_dso(filename, readelf, excludes)
  else:
    print("WARN: Could not recognize ELF file type for", filename)


def check_vtables_in_dir(dirname, readelf, excludes):
  assert os.path.exists(dirname), "{} does not exist".format(dirname)
  for root, _, files in os.walk(dirname):
    for file in files:
      path = os.path.join(root, file)
      check_vtables_in_file(path, readelf, excludes)


def check_file_main(args):
  filename = args.filename
  excludes = set(args.exclude)

  if os.path.isfile(filename):
    check_vtables_in_file(filename, args.readelf, excludes)
  else:
    check_vtables_in_dir(filename, args.readelf, excludes)

  return 0


def check_toolchain_main(args):
  excludes = set(args.exclude)
  check_vtables_in_dir(
      os.path.join(args.toolchain_dir, "lib", "aarch64-unknown-fuchsia"),
      args.readelf, excludes)
  check_vtables_in_dir(
      os.path.join(args.toolchain_dir, "lib", "x86_64-unknown-fuchsia"),
      args.readelf, excludes)
  return 0


def check_toolchain_multilib_main(args):
  excludes = set(args.exclude)

  targets = (
      "aarch64-unknown-fuchsia",
      "x86_64-unknown-fuchsia",
  )
  multilibs = (
      "relative-vtables",
      "relative-vtables+noexcept",
      "relative-vtables+asan",
      "relative-vtables+asan+noexcept",
  )

  for target in targets:
    for multilib in multilibs:
      p = Path(args.toolchain_dir) / "lib" / target / multilib
      check_vtables_in_dir(p, args.readelf, excludes)

  # Check hwasan if exists
  p = Path(
      args.toolchain_dir
  ) / "lib" / "aarch64-unknown-fuchsia" / "relative-vtables+hwasan"
  if p.exists():
    check_vtables_in_dir(p, args.readelf, excludes)
  p = Path(
      args.toolchain_dir
  ) / "lib" / "aarch64-unknown-fuchsia" / "relative-vtables+hwasan+noexcept"
  if p.exists():
    check_vtables_in_dir(p, args.readelf, excludes)

  return 0


def check_fuchsia_main(args):
  excludes = set(args.exclude)

  # Read files to check from binaries.json.
  with open(os.path.join(args.build, "binaries.json"), "r") as f:
    import json
    binaries = json.load(f)

  for binary in binaries:
    if binary["os"] != "fuchsia":
      continue

    path = os.path.join(args.build, binary["debug"])
    if os.path.exists(path):
      check_vtables_in_file(path, args.readelf, excludes)
    else:
      print(f"Skipping {path}. This path does not exist.")

  return 0


def main():
  parser = argparse.ArgumentParser(
      description=
      "Script for checking that all vtables in an ELF file are in .rodata",
      formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("--readelf",
                      default=DEFAULT_READELF,
                      help="readelf executable to use")
  parser.add_argument("--exclude",
                      action="append",
                      default=[],
                      help="Do not check if this class is in rodata.")

  subparsers = parser.add_subparsers(dest="mode",
                                     help="Choose which mode this tool should run with.")

  file_parser = subparsers.add_parser("file",
                                      description="Check vtables in a single ELF file.")
  file_parser.add_argument(
      "filename",
      type=os.path.abspath,
      help=
      "Path to ELF file to or directory to recursively search through for ELF files"
  )

  toolchain_parser = subparsers.add_parser("toolchain",
      description="""
Script for checking that all vtables in an ELF file are in .rodata. This will
check all relevant fuchsia-target executables and DSOs in a clang toolchain
(libc++, libunwind, etc). This includes:

- ${toolchain_dir}/lib/aarch64-unknown-fuchsia/
- ${toolchain_dir}/lib/x86_64-unknown-fuchsia/
""",
      formatter_class=RawDescriptionDefaultArgsHelpFormatter)
  toolchain_parser.add_argument("toolchain_dir",
                      type=os.path.abspath,
                      help="Path to the clang toolchain build directory")

  fuchsia_parser = subparsers.add_parser("fuchsia",
      description="""
Script for checking that all vtables in an ELF file are in .rodata. This will
check all relevant fuchsia-target executables and DSOs in a fuchsia build.
""",
      formatter_class=RawDescriptionDefaultArgsHelpFormatter)
  fuchsia_parser.add_argument(
      "build",
      type=os.path.abspath,
      help="Path to either the GN or ZN fuchsia build directory.")

  multilib_parser = subparsers.add_parser("multilib",
      description="""
Script for checking that all vtables in an ELF file are in .rodata. This will
check all relevant fuchsia-target executables and DSOs in a clang toolchain
(libc++, libunwind, etc). This includes:

Script for checking that all vtables in an ELF file are in .rodata. This will
check all relevant fuchsia-target executables and DSOs in the relative-vtables
multilib variant of a clang toolchain (libc++, libunwind, etc). This includes:

- ${toolchain_dir}/lib/x86_64-unknown-fuchsia/relative-vtables/
- ${toolchain_dir}/lib/x86_64-unknown-fuchsia/relative-vtables+noexcept/
- ${toolchain_dir}/lib/x86_64-unknown-fuchsia/relative-vtables+asan/
- ${toolchain_dir}/lib/x86_64-unknown-fuchsia/relative-vtables+asan+noexcept/
- ${toolchain_dir}/lib/aarch64-unknown-fuchsia/relative-vtables/
- ${toolchain_dir}/lib/aarch64-unknown-fuchsia/relative-vtables+noexcept/
- ${toolchain_dir}/lib/aarch64-unknown-fuchsia/relative-vtables+asan/
- ${toolchain_dir}/lib/aarch64-unknown-fuchsia/relative-vtables+asan+noexcept/
- ${toolchain_dir}/lib/aarch64-unknown-fuchsia/relative-vtables+hwasan/
- ${toolchain_dir}/lib/aarch64-unknown-fuchsia/relative-vtables+hwasan+noexcept/
""",
      formatter_class=RawDescriptionDefaultArgsHelpFormatter)
  multilib_parser.add_argument("toolchain_dir",
                      type=os.path.abspath,
                      help="Path to the clang toolchain build directory")

  file_parser.set_defaults(func=check_file_main)
  toolchain_parser.set_defaults(func=check_toolchain_main)
  multilib_parser.set_defaults(func=check_toolchain_multilib_main)
  fuchsia_parser.set_defaults(func=check_fuchsia_main)

  args = parser.parse_args()
  return args.func(args)


if __name__ == "__main__":
  import sys
  sys.exit(main())
