#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Suggests OWNERS for projects.

For each project in a given jiri manifest file or at a given path, do the
following:
1. Check if it has an OWNERS file up the path (or at the root path only when
   run with `--skip_root_owners_only`). If so, skip.
2. Find references to the project via `gn refs`. If none, skip.
3. Find immediate owners for all referrers. If none, for each referrer travel
   one directory up and continue the search. Ignore root owners.

Example usage:
$ fx set ...
$ scripts/owner/suggest_owners.py \
--jiri_manifest integration/third_party/flower --csv csv.out

$ scripts/owner/suggest_owners.py --path third_party/crashpad --csv csv.out

$ scripts/owner/suggest_owners.py --path third_party/* --csv csv.out

$ scripts/owner/suggest_owners.py --path third_party/* --all_refs --csv csv.out

$ scripts/owner/suggest_owners.py --path third_party/* --all_refs --generate_missing

$ scripts/owner/suggest_owners.py \
    --jiri_manifest integration/third_party/flower --generate_missing --dry_run

$ scripts/owner/suggest_owners.py \
    --jiri_manifest integration/third_party/flower --skip_root_owners_only
"""

import argparse
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

# Matches a valid email address, more or less.
EMAIL = re.compile("^[\w%+-]+@[\w.-]+\.[a-zA-Z]{2,}$")


def maybe_run(dry_run, *command, **kwargs):
    if not dry_run:
        return check_output(*command, **kwargs)
    print("Dry-run: ", command)


def check_output(*command, **kwargs):
    try:
        return subprocess.check_output(
            command, stderr=subprocess.STDOUT, encoding="utf8", **kwargs)
    except subprocess.CalledProcessError as e:
        print("Failed: " + " ".join(command), file=sys.stderr)
        print(e.output, file=sys.stderr)
        raise e


def get_project_paths(jiri_manifest_path):
    manifest = ET.parse(jiri_manifest_path)
    root = manifest.getroot()
    projects = root.find("projects")
    return [project.get("path") for project in projects.findall("project")]


def get_referencing_paths(*args):
    build_dir = check_output("fx", "get-build-dir").strip()
    try:
        refs_out = check_output("fx", "gn", "refs", build_dir, *args)
    except Exception as e:
        print(f"Failed to find refs to {args}", file=sys.stderr)
        print(e.output, file=sys.stderr)
        return []
    # Remove empty lines, turn labels to paths, unique, sort
    return sorted(
        {line[2:].partition(":")[0] for line in refs_out.splitlines() if line})


def get_filenames(path):
    filenames = []
    for dirpath, dirnames, names in os.walk(path):
        for name in names:
            filepath = os.path.join(dirpath, name)
            filenames.append(filepath)
    return filenames


def get_owners(path):
    owners_path = os.path.join(path, "OWNERS")
    if not os.path.exists(owners_path):
        return set()
    owners = set()
    for line in open(owners_path):
        line = line.strip()
        if line.startswith("include "):
            owners.update(
                get_owners(line[len("include /"):(len(line) - len("/OWNERS"))]))
        elif line.startswith("per-file "):
            owners.update(
                line[len("per-file "):].partition("=")[2].strip().split(","))
        elif line.startswith("#"):
            continue
        else:
            owners.add(line)
    # Remove stuff that doesn't look like an email address
    return {owner for owner in owners if EMAIL.match(owner)}


def find_owners_file(path, recursive=True):
    # Look for the OWNERS file in the given path.
    owners_path = os.path.join(path, "OWNERS")
    if os.path.exists(owners_path):
        return owners_path
    if not recursive:
        return None
    # If not found, search one directory up.
    parent_path = os.path.dirname(path)
    if parent_path and parent_path != path:
        return find_owners_file(parent_path)
    return None


def generate_owners_file(project_path, refs, owners, dry_run):
    # Find and include the OWNERS files of all references.
    includes = set()
    for ref in refs:
        path = find_owners_file(ref)
        if path:
            includes.add("include /" + path + "\n")

    # Write the OWNERS file.
    owners_file = os.path.join(project_path, "OWNERS")
    content = "".join(sorted(includes))
    if dry_run:
        print(f"Dry run: generating {owners_file} with content:\n{content}")
    else:
        print(f"Generating {owners_file}...")
        file = open(owners_file, "w")
        file.write(content)
        file.close()

    # `git add` the OWNERS file.
    try:
        maybe_run(dry_run, "git", "add", "OWNERS", "-v", cwd=project_path)
    except Exception as e:
        print(f"Failed to `git add` OWNERS file.", file=sys.stderr)
        print(e.output, file=sys.stderr)
        os.remove(owners_path)
        return

    # Commit the OWNERS file.
    try:
        commit_message = '''[owners] Add OWNERS file

Add as owners the owners of the code that calls into this dependency.

The OWNERS file and this commit are generated by
//scripts/owners/suggest_owners.py.

Bug: 102810
'''
        maybe_run(
            dry_run, "git", "commit", "-m", commit_message, cwd=project_path)
    except Exception as e:
        print(f"Failed to commit OWNERS file.", file=sys.stderr)
        print(e.output, file=sys.stderr)
        maybe_run("git", "restore", "--staged", "OWNERS", cwd=project_path)
        os.remove(generate_owners_file)
        return

    # Upload the CL for review.
    try:
        reviewers = ",".join(owners)
        if not dry_run:
            # Prompt for confirmation to continue.
            choice = input(
                f"Uploading {owners_file} for review and adding "
                f"{reviewers} as reviewers. Do you want to continue? [y/N] ")
            if not choice.lower()[:1] == 'y':
                check_output("git", "checkout", "origin/main", cwd=project_path)
                return

        upload_out = maybe_run(
            dry_run,
            "jiri",
            "upload",
            "-remoteBranch=main",
            "-r=" + reviewers,
            cwd=project_path)
        if upload_out:
            print(upload_out)
    except Exception as e:
        print(f"Failed to upload CL for review.", file=sys.stderr)
        print(e.output, file=sys.stderr)
        maybe_run("git", "checkout", "origin/main", cwd=project_path)


def main():
    parser = argparse.ArgumentParser(description="Suggests owners for projects")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--jiri_manifest", help="Input jiri manifest file")
    group.add_argument(
        "--path",
        nargs='+',
        help="Input project path, relative to fuchsia root")
    parser.add_argument(
        "--all_refs",
        action='store_true',
        help=
        "Search for references to all targets and all files in input projects")
    parser.add_argument(
        "--csv", help="Output csv file", type=argparse.FileType('w'))
    parser.add_argument(
        "--generate_missing",
        action='store_true',
        help=
        "Generates OWNERS files for projects missing owners and uploads them for review."
    )
    parser.add_argument(
        "--dry_run",
        action='store_true',
        help=
        "Used in conjunction with `--generate_missing` to print the generated OWNERS file "
        "and git/jiri commands instead of creating the file and running the commands."
    )
    parser.add_argument(
        "--skip_root_owners_only",
        action='store_true',
        default=False,
        help="Skips project only if it has an OWNERS files at the root path, and "
        "not in parent directories.")
    args = parser.parse_args()
    if args.dry_run and not args.generate_missing:
        parser.error(
            "--dry_run: not allowed without argument --generate_missing")

    # Set working directory to //
    fuchsia_root = os.path.dirname(  # //
        os.path.dirname(             # scripts/
        os.path.dirname(             # owner/
        os.path.abspath(__file__))))
    os.chdir(fuchsia_root)

    if args.jiri_manifest:
        project_paths = get_project_paths(args.jiri_manifest)
    else:
        project_paths = [
            project.strip("/")
            for project in args.path
            if os.path.isdir(project)
        ]

    for project_path in project_paths:
        # Skip if the project has OWNERS.
        if owners_file := find_owners_file(project_path,
                                           not args.skip_root_owners_only):
            print(f"{project_path} has owners at {owners_file}, skipping.")
            continue

        # If the path ends in `/src`, use the path without the `/src` ending to
        # search for references and generate OWNERS file (if configured).
        if project_path.endswith("/src"):
            project_path = project_path[:-len("/src")]

        # Search for references to any of the project's targets if `--all_refs`
        # is set, or for the top-level targets otherwise.
        refs = get_referencing_paths(
            project_path + ("/*" if args.all_refs else ":*"))
        if not refs and args.all_refs:
            print(
                f"{project_path} has no target references, searching for all file references."
            )
            files = get_filenames(project_path)
            refs = get_referencing_paths(project_path, *files)
        if not refs:
            print(f"{project_path} has no references, skipping.")
            continue
        # Filter // and internal references (paths inside the project)
        refs = {ref for ref in refs if ref and not ref.startswith(project_path)}

        owners = set()
        while not owners and refs:
            for ref in refs:
                owners.update(get_owners(ref))
            if not owners:
                # Go one directory up, terminating before //
                refs = {os.path.dirname(ref) for ref in refs}
                refs = {
                    ref for ref in refs
                    if ref and not ref.startswith(project_path)
                }
        if not owners:
            print(f"{project_path} not referenced by anything owned, skipping.")
            continue

        print()
        print(f"{project_path} suggested owners:")
        print("\n".join(sorted(owners)))
        print()
        print(f"This is based on incoming references from:")
        print("\n".join(sorted(refs)))
        print()
        if args.csv:
            args.csv.write(f"{project_path},{owners},{refs}\n")

        if args.generate_missing:
            generate_owners_file(project_path, refs, owners, args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
