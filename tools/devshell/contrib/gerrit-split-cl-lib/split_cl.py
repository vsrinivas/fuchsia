#!/usr/bin/env python3
#
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import traceback

from pathlib import Path
from typing import Any, Dict, List, Optional, Set

DEFAULT_REPO = "https://fuchsia-review.googlesource.com/fuchsia"

USAGE_DESCRIPTION = r"""
Split a single CL into multiple CLs and upload each of those CLs to Gerrit.
This command should be run from the directory containing your Git checkout.

For example, if you've created a single large CL with Gerrit CL number 1234
and the most recent patchset number is 4, you can split that into multiple
CLs with the following command:

  split_cl.py --cl=12345 --patch=4

This script will:

  1. Download the specified patchset to your local Git checkout.

  2. Determine the set of files changed by that patchset, then open your
     $EDITOR with a configuration file that allows you to specify how the
     patchset should be split into multiple CLs. Comments at the top of
     the configuration file explain how this is done. When you are satisfied,
     save the file and close your $EDITOR.

  3. Create a local Git branch for each CL specified in the prior step.

  4. Upload each Git branch to gerrit. The uploaded CLs will use the same
     commit message as the original CL, except the first line will be
     prefixed by one or more tags (such as "[fidl]") which are automatically
     determined based on the set of changed files. All CLs will be tagged
     with the same gerrit topic.

  5. Finally, after all CLs are uploaded, the script will checkout JIRI_HEAD.
"""


def parse_args() -> Any:
  parser = argparse.ArgumentParser(
    description=USAGE_DESCRIPTION,
    formatter_class=argparse.RawDescriptionHelpFormatter,
  )
  parser.add_argument(
     "--dry_run",
     dest="dry_run",
     action="store_true",
     help="If true, print commands that would run but don't execute any of them.",
     required=False,
  )
  parser.add_argument(
      "--cl",
      dest="cl",
      metavar="CL",
      type=int,
      help="Gerrit CL to split. Must be a Gerrit CL number.",
      required=True,
  )
  parser.add_argument(
      "--patch",
      dest="patch",
      metavar="N",
      type=int,
      help="Patch number of the CL to split.",
      required=True,
  )
  parser.add_argument(
      "--repo",
      dest="repo",
      help=f"Gerrit repo to connect to. Defaults to {DEFAULT_REPO}",
      default=DEFAULT_REPO,
      required=False,
  )
  return parser.parse_args()


def run_and_get_stdout(cmd: List[str]) -> str:
  """
  Run the given command and return its stdout.
  """
  print(f"Running {cmd}")
  return subprocess.run(cmd, stdout=subprocess.PIPE, check=True).stdout.decode("utf-8")


class Splitter:
  def __init__(self, dry_run: bool, repo: str, cl: int, patch: int):
    self.dry_run = dry_run
    self.repo = repo
    self.cl = cl
    self.patch = patch
    self.branch = self.gerrit_branch()
    self.topic = f"auto-batch-{cl}"

    fetch_cmd = f'git fetch {self.repo} {self.branch}'.split(' ')
    checkout_cmd = 'git checkout FETCH_HEAD'.split(' ')
    diff_cmd = 'git diff --name-only HEAD~'.split(' ')
    subject_cmd = 'git log -n 1 --format=format:%s'.split(' ')
    body_cmd = 'git log -n 1 --format=format:%b'.split(' ')

    # Checkout the CL so we can learn the files changed and the CL description.
    print("Loading CL info...")
    run_and_get_stdout(fetch_cmd)
    run_and_get_stdout(checkout_cmd)
    diff_stdout = run_and_get_stdout(diff_cmd)
    self.paths_changed = [
        Path(line.strip())
        for line in diff_stdout.split('\n')
        if line.strip() != ""
    ]
    self.subject = run_and_get_stdout(subject_cmd)
    self.description = run_and_get_stdout(body_cmd)
    self.description = re.compile('Change-Id:.*').sub('', self.description)
    print()


  def gerrit_branch(self) -> str:
    """
    Return the Gerrit branch name for the given CL and patch. See:
    https://gerrit-review.googlesource.com/Documentation/concept-refs-for-namespace.html
    """
    cl_last_digits = self.cl % 100
    return f'refs/changes/{cl_last_digits:02d}/{self.cl}/{self.patch}'


  def maybe_run(self, cmd: List[str], input: Any = None, must_succeed: bool = True) -> Optional[subprocess.CompletedProcess]:
    """
    If self.dry_run, just print the command, otherwise run it.
    """
    print(f"Running {cmd}")
    if self.dry_run:
      return None
    return subprocess.run(cmd, stdout=subprocess.PIPE, input=input, check=must_succeed)


  def create_commit(self, tags: List[str], files: List[Path]):
    tags_str = "".join([f"[{tag}]" for tag in tags])
    commit_subject = f"{tags_str} {self.subject}"
    branch = f"{self.topic}-{'-'.join(tags)}-{get_stable_hash([str(f) for f in files])}"

    print()
    print(f"Creating commit: {commit_subject}")
    print(f"{len(files)} files:")
    for file in files:
      print(f"  {file}")

    self.maybe_run(["git", "--no-pager", "checkout", "JIRI_HEAD"])
    self.maybe_run(["git", "--no-pager", "branch", "-D", branch], must_succeed=False)
    self.maybe_run(["git", "--no-pager", "checkout", "-b", branch])
    self.maybe_run(["git", "--no-pager", "fetch", self.repo, self.branch])

    diff = self.maybe_run(["git", "--no-pager", "show", "FETCH_HEAD", "--"] +
                          [str(f) for f in files])
    self.maybe_run(["git", "--no-pager", "apply", "--whitespace=fix", "--3way", "-"],
                   input=(diff.stdout if diff else None))

    self.maybe_run(["git", "--no-pager", "commit", "-m", commit_subject, "-m", self.description])
    self.maybe_run(["git", "--no-pager", "push", "origin", "HEAD:refs/for/main", "-o",
                    f"topic={self.topic}"])
    self.maybe_run(["git", "--no-pager", "checkout", "JIRI_HEAD"])


def get_cl_tags(files: List[Path]) -> List[str]:
  """
  Come up with descriptive tags given a list of files changed.

  For each path, use the first rule that applies in the following order:

  1) Pick the path component right after the last "lib", "bin", "drivers",
     or "devices".
  2) If the path begins with "src", then
     - if the third path component is "tests" then pick the fourth component
     - pick the third path component, e.g. src/developer/shell -> shell
  3) If the path begins with "zircon", then pick the path component after
     either "ulib" or "utest", e.g.
     zircon/system/ulib/fdio-caller/test/fdio.cc -> fdio-caller
  4) If the path begins with "examples" or "tools", then pick the next path
     component, e.g. examples/fidl/llcpp/async_completer/client/main.cc -> fidl

  Example:

    get_cl_tags([
      "src/lib/loader_service/loader_service_test.cc",
      "src/lib/loader_service/loader_service_test_fixture.cc",
    ]) == ["loader_service"]

  """

  def get_tag(p: Path) -> str:
    if p.parts[0] == "examples" or p.parts[0] == "tools":
      return p.parts[1]
    tag: str = ""
    for part, next_part in zip(p.parts, p.parts[1:]):
      if (
        part == "lib"
        or part == "bin"
        or part == "drivers"
        or part == "devices"
      ):
        if next_part != "tests" and not next_part.endswith(".cc"):
          tag = next_part
    if tag != "":
      return tag
    if p.parts[0] == "build":
      return "build"
    if p.parts[0] == "src":
      if len(p.parts) >= 3:
        if p.parts[2] == "tests" and not p.parts[3].endswith(".cc"):
          return p.parts[3]
        return p.parts[2]
    if p.parts[0] == "zircon":
      for part, next_part in zip(p.parts, p.parts[1:]):
        if part == "ulib" or part == "utest":
          return next_part
    raise RuntimeError(f"Could not infer tags from path {p}")

  tags: Set[str] = set()
  for file in files:
    tags.add(get_tag(file))
  return sorted(list(tags))


def get_stable_hash(thing):
  return hashlib.sha1(json.dumps(thing).encode("utf-8")).digest().hex()


def main() -> int:
  # Parse and validate arguments.
  args = parse_args()
  splitter = Splitter(args.dry_run, args.repo, args.cl, args.patch)

  file_groups: Dict[Path, List[Path]] = {}
  for file in splitter.paths_changed:
    if file.parent not in file_groups:
      file_groups[file.parent] = []
    file_groups[file.parent].append(file)

  print(f"Found {len(file_groups)} folders...")
  with tempfile.NamedTemporaryFile("w") as tmp:
    tmp.write(
      f"""
# Consecutive lines will be combined into the same CL.
# By default, files are grouped by their immediate parent folder.
# When you are satisfied, save this file and close your editor.
""".strip()
    )
    tmp.write("\n\n")
    for stem, files in file_groups.items():
      for file in files:
        tmp.write(f"{file}\n")
      tmp.write("\n")
    tmp.flush()
    editor = os.getenv('EDITOR', 'vim')
    os.system(f"{editor} {tmp.name}")
    with open(tmp.name) as tmp_read:
      lines = [
        line.strip()
        for line in tmp_read.readlines()
        if not line.startswith("#")
      ]

  change_lists: List[List[Path]] = []
  current_change: List[Path] = []
  for line in lines:
    if line == "":
      if len(current_change) > 0:
        change_lists.append(current_change)
        current_change = []
    else:
      current_change.append(Path(line))

  for change in change_lists:
    splitter.create_commit(get_cl_tags(change), change)
    if not splitter.dry_run:
      # This sleep is intended to reduce the change that gerrit DoS-blocks our
      # requests. The gerrit server's DoS config is unknown, so this may be
      # insufficient in some cases.
      print("Sleeping for 5s to throttle gerrit requests...")
      time.sleep(5)


if __name__ == "__main__":
  try:
    sys.exit(main())
  except Exception as e:
    traceback.print_exception(e, tb=None, value=None)
    print(f"Error: {e}")
  sys.exit(1)
