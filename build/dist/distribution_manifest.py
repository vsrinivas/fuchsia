#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A collection of functions to process distribution manifests. These are JSON
# files that contain a a list of objects, which follow the schema documented
# in //build/dist/distribution_manifest.gni

import collections
import dataclasses
import filecmp
import json
import os

from typing import Any, Callable, Iterable, List, Set, DefaultDict, Dict, Optional, Tuple

# A namedtuple type used to model an entry from a distribution manifest, after
# expansion.
Entry = collections.namedtuple('Entry', ['destination', 'source', 'label'])


@dataclasses.dataclass
class ParseResult:
    """A class modelling the result of parsing a partial manifest."""

    # The list of distribution Entry items.
    entries: List[Entry]

    # The list of parsing errors. Empty if none.
    errors: List[str]

    # A { destination_path -> elf_runtime_dir } map.
    elf_runtime_map: Dict[str, str]


# This supports distribution manifest as JSON files which are lists of objects
# (dictionaries in Python), fully documented at:
# //docs/concepts/build_system/internals/manifest_formats.md
#
# Note that it is possible for an expanded manifest to have several entries
# for the same destination path. If all entries have the same source
# (either the same source path, or the same source content!), then they
# can be merged into a single one. Otherwise, it is a build error.
#

PartialEntry = Dict[str, str]


def expand_manifest_items_inner(
    manifest_items: Iterable[PartialEntry],
    opened_files: Set[str],
    default_label: Optional[str] = None
) -> Tuple[List[Entry], List[PartialEntry]]:
    """Expand the content of a distribution manifest file.

    Note that this function does not try to de-duplicate identical entries.

    Args:
        manifest_items: A list of dictionaries, corresponding to the
            content of a manifest file.
        opened_files: A set of file paths, which will be updated with
            the paths of the files that have been read during expansion.
        default_label: An optional string that will be used as a default
            "label" value if an entry does not have one.
    Returns:
        An (entries, extras) tuple, where `entries` is an Entry list,
        and `extras` is a list of input items that need further processing,
        e.g. renaming entries.
    """
    entries: List[Entry] = []
    extras: List[PartialEntry] = []
    if manifest_items is None:
        return entries, extras
    for item in manifest_items:
        if 'label' not in item and default_label is not None:
            item['label'] = default_label
        if 'renamed_source' in item:
            # A renaming entry, for now just add it to the 'extras' list to
            # be processed by the caller.
            extras.append(item)
        if 'copy_from' in item:
            # A copy entry, for now just add it to the 'extras' list.
            extras.append(item)
        if 'source' in item:
            if 'elf_runtime_dir' in item:
                # Save the entry in 'extras', to be parsed later then delete
                # the key from the item.
                extras.append(item.copy())
                del item['elf_runtime_dir']

            entries.append(Entry(**item))

        elif 'file' in item:
            file_path = item['file']
            item_label = item['label']
            opened_files.add(file_path)
            with open(file_path) as data_file:
                data = json.load(data_file)
            new_entries, new_extras = expand_manifest_items_inner(
                data, opened_files, item_label)
            entries += new_entries
            extras += new_extras

    return entries, extras


def expand_partial_manifest_items(
        manifest_items: Iterable[PartialEntry],
        opened_files: Set[str],
        default_label: Optional[str] = None) -> ParseResult:
    """Expand the content of a distribution manifest file.

    Note that this function does not try to de-duplicate identical entries.

    Args:
        manifest_items: A list of dictionaries, corresponding to the
            content of a manifest file.
        opened_files: A set of file paths, which will be updated with
            the paths of the files that have been read during expansion.
        default_label: An optional string that will be used as a default
            "label" value if an entry does not have one.
    Returns:
        An (entries, errors) tuple, where `entries` is an Entry list, and
        `errors` is a list of string describing errors found in the input,
        which will be empty on success.
    """
    entries, extras = expand_manifest_items_inner(
        manifest_items, opened_files, default_label)

    # Process extra entries here.
    errors: List[str] = []
    unknown_renames: List[PartialEntry] = [
    ]  # rename entries with unknown renamed_source path.
    renamed_entries: List[Entry] = []
    renamed_sources: Set[str] = set(
    )  # Source paths of original entries that are renamed.
    persistent_sources: Set[str] = set(
    )  # Source paths of original entries that must be preserved.

    elf_runtime_map: Dict[str, str] = {}  # Map destination path to the
    # corresping elf runtime directory.

    if extras:
        # Verify that each renaming entry references a given regular entry.
        source_entry_map = {e.source: e for e in entries}

        # A map that associates with each destination path (e.g. 'bin/foo')
        # the extra items that have an elf_runtime_dir key in it.
        elf_runtime_entries: Dict[str,
                                  List[Dict]] = collections.defaultdict(list)

        # A map built from all copy entries, that maps their destination path
        # to the corresponding source path.
        copy_reverse_map = {
            e['copy_to']: e['copy_from'] for e in extras if 'copy_from' in e
        }

        for extra in extras:
            if 'renamed_source' in extra:
                source = extra['renamed_source']
                dest = extra['destination']

                source_entry = source_entry_map.get(source)
                if source_entry is None:
                    # Try with the copy entries.
                    alt_source = copy_reverse_map.get(source)
                    if alt_source:
                        source = alt_source
                        source_entry = source_entry_map.get(source)

                if source_entry is None:
                    unknown_renames.append(extra)
                    continue

                new_entry = source_entry._replace(destination=dest)
                extra_label = extra.get('label')
                if extra_label:
                    new_entry = new_entry._replace(label=extra_label)
                renamed_entries.append(new_entry)
                renamed_sources.add(source)
                if extra.get('keep_original', False):
                    persistent_sources.add(source)

            elif 'copy_from' in extra:
                # Already handled by copy_reverse_map above.
                pass

            elif 'elf_runtime_dir' in extra:
                dest = extra['destination']
                elf_runtime_entries[dest].append(extra)
                pass

            else:
                # Should not happen unless there is a bug in
                # expand_manifest_entries_inner.
                assert False, 'Unsupported extra item: %s' % extra

        if elf_runtime_entries:
            # For each destination path, there should be a single ELF runtime dir,
            # so try to find conflicts here.
            elf_conflicts = []
            for dest, extras in elf_runtime_entries.items():
                elf_dirs = set(e['elf_runtime_dir'] for e in extras)
                if len(elf_dirs) > 1:
                    elf_conflicts += list(extras)
                else:
                    assert len(elf_dirs) == 1
                    elf_runtime_map[dest] = elf_dirs.pop()

            if elf_conflicts:
                errors.append(
                    'ERROR: Entries with same destination path have different ELF runtime dir:'
                )
                for entry in sorted(elf_conflicts,
                                    key=lambda x: x['destination']):
                    errors.append(
                        '  - destination=%s source=%s label=%s elf_runtime_dir=%s'
                        % (
                            entry['destination'], entry['source'],
                            entry['label'], entry['elf_runtime_dir']))

    if unknown_renames:
        errors.append(
            'ERROR: Renamed distribution entries have unknown source destination:'
        )
        for extra in unknown_renames:
            errors.append('  - %s' % json.dumps(extra))

    # When the source path of a copy entry is actually provided by several
    # regular entries, it means that one of the latter comes from a resource()
    # target, instead of a renamed_binary() one. Unfortunately, there is no
    # way to know from the input data which regular entry should be preserved.
    #
    # For example:
    #
    #   resource("bar") {
    #     outputs = [ "bin/bar" ]
    #     deps = [ "//src:foo" ]
    #     sources = [ "$root_build_dir/foo" ]
    #   }
    #
    #   renamed_binary("zoo") {
    #     dest = "bin/zoo"
    #     source = "$root_build_dir/foo"
    #     deps = [ "//src:foo" ]
    #   }
    #
    # Would generate:
    #
    #   {
    #     "destination": "bin/bar",
    #     "source": "foo",
    #     "label": "//whatever:bar",
    #   }
    #
    #   {
    #     "destination": "bin/foo",
    #     "source": "foo",
    #     "label": "//src:foo",
    #   }
    #
    #   {
    #      renamed_from = "foo",
    #      destination: "bin/zoo",
    #      label = "//whatever:zoo"
    #   }
    #
    # Notice that from the data above, it's impossible to tell whether to
    # remove the first or second entry from the final manifest.
    #
    # Since this is a seldom case, detect it here and generate an error
    # message that explains how to solve the issue.
    #
    source_to_multi_entries: Dict[str,
                                  Set[Entry]] = collections.defaultdict(set)
    for e in entries:
        source_to_multi_entries[e.source].add(e)

    multi_source_entries = []
    for src, src_entries in source_to_multi_entries.items():
        if src in renamed_sources and len(src_entries) > 1:
            multi_source_entries += list(src_entries)

    if multi_source_entries:
        errors.append(
            'ERROR: Multiple regular entries with the same source path:')
        for e in sorted(multi_source_entries):
            errors.append(
                '  - destination=%s source=%s label=%s' %
                (e.destination, e.source, e.label))
        errors.append(
            '\nThis generally means a mix of renamed_binary() and resource() targets\n'
            +
            'that reference the same source. Try replacing the resource() targets by\n'
            + 'renamed_binary() ones to fix the problem\n')

    renamed_sources -= persistent_sources
    entries = [
        e for e in entries if e.source not in renamed_sources
    ] + renamed_entries

    return ParseResult(
        entries=entries, errors=errors, elf_runtime_map=elf_runtime_map)


def expand_manifest_items(
        manifest_items: Iterable[PartialEntry],
        opened_files: Set[str],
        default_label: Optional[str] = None) -> List[Entry]:
    """Expand the content of a distribution manifest file.

    Note that this function does not try to de-duplicate identical entries.

    Args:
        manifest_items: A list of dictionaries, corresponding to the
            content of a manifest file.
        opened_files: A set of file paths, which will be updated with
            the paths of the files that have been read during expansion.
        default_label: An optional string that will be used as a default
            "label" value if an entry does not have one.
    Returns:
        An Entry list.
    """
    result = expand_partial_manifest_items(
        manifest_items, opened_files, default_label)
    if result.errors:
        raise Exception('\n'.join(result.errors))
    return result.entries


def _entries_have_same_source(
        entry1: Entry, entry2: Entry, opened_files: Set[str]) -> bool:
    """Return True iff two entries have the same source.

    Args:
        entry1, entry2: input entries to compare.
        opened_files: a set of file paths, updated with the input entries'
            source paths if they need to be opened for comparing their
            content.
    Returns:
        True iff the entries have the same source path, or if the
        path point to files with the same content.
    """
    if entry1.source == entry2.source:
        return True

    opened_files.add(entry1.source)
    opened_files.add(entry2.source)
    return filecmp.cmp(entry1.source, entry2.source)


def expand_manifest(
        manifest_items: Iterable[Dict[str, str]],
        opened_files: Set[str]) -> Tuple[List[Entry], str]:
    """Expand the content of a distribution manifest into an Entry list.

    Note, this removes duplicate entries, if they have the same source
    path or content, and will report conflicts otherwise.

    Args:
        input_entries: An Entry list, that may contain duplicate entries.
            Note that two entries are considered duplicates if they
            have the same destination, and the same source (either by
            path or content).
        opened_files: A set of file paths, which will be updated with
            the paths of the files that have been read during the merge.
    Returns:
        A (merged_entries, error_msg) tuple, where merged_entries is an
        Entry list of the merged input entries, and error_msg is a string
        of error messages (which happen when conflicts are detected), or
        an empty string in case of success.
    """
    input_entries = expand_manifest_items(manifest_items, opened_files)

    # Used to record that a given destination path has two or more conflicting
    # entries, with different sources.
    source_conflicts: DefaultDict[str,
                                  Set[Entry]] = collections.defaultdict(set)

    dest_to_entries: Dict[str, Entry] = {}
    for entry in input_entries:
        dest = entry.destination
        current_entry = dest_to_entries.setdefault(dest, entry)
        if current_entry == entry:
            continue

        if not _entries_have_same_source(entry, current_entry, opened_files):
            source_conflicts[dest].update((current_entry, entry))
            continue

        # These entries have the same source path, so merge them.
        if current_entry.label is None:
            dest_to_entries[dest] = current_entry._replace(label=entry.label)

    error = ""
    for dest, entries in source_conflicts.items():
        error += "  Conflicting source paths for destination path: %s\n" % dest
        for entry in sorted(entries, key=lambda x: x.source):
            error += "   - source=%s label=%s\n" % (entry.source, entry.label)

    if error:
        error = 'ERROR: Conflicting distribution entries!\n' + error

    return (
        sorted(dest_to_entries.values(), key=lambda x: x.destination), error)


def distribution_entries_to_string(entries: List[Entry]) -> str:
    """Convert an Entry list to a JSON-formatted string."""
    return json.dumps(
        [e._asdict() for e in sorted(entries)],
        indent=2,
        sort_keys=True,
        separators=(',', ': '))


def convert_fini_manifest_to_distribution_entries(
        fini_manifest_lines: Iterable[str], label: str) -> List[Entry]:
    """Convert a FINI manifest into an Entry list.

    Args:
        fini_manifest_lines: An iteration of input lines from the
            FINI manifest.
        label: A GN label that will be applied to all generated
            entries in the resulting list.
    Returns:
        An Entry list.
    """
    result: List[Entry] = []
    for line in fini_manifest_lines:
        dst, _, src = line.strip().partition('=')
        entry = Entry(destination=dst, source=src, label=label)
        result.append(entry)

    return result


def _rewrite_elf_needed(dep: str) -> Optional[str]:
    """Rewrite an ELF DT_NEEDED dependency name.

    Args:
        dep: dependency name as it appears in ELF DT_NEEDED entry (e.g. 'libc.so')
    Returns:
        None if the dependency should be ignored, or the input dependency name,
        possibly rewritten for specific cases (e.g. 'libc.so' -> 'ld.so.1')
    """
    if dep == 'libzircon.so':
        # libzircon.so being injected by the kernel into user processes, it should
        # not appear in Fuchsia packages, and thus should be ignored.
        return None
    if dep == 'libc.so':
        # ld.so.1 acts as both the dynamic loader and C library, so any reference
        # to libc.so should be rewritten as 'ld.so.1'
        return 'ld.so.1'

    # For all other cases, just return the unmodified dependency name.
    return dep


def verify_elf_dependencies(
    binary_name: str,
    lib_dir: str,
    deps: Iterable[str],
    get_lib_dependencies: Callable[[str], Optional[List[str]]],
    visited_libraries: Set[str] = set()
) -> List[str]:
    """Verify the ELF dependencies of a given ELF binary.

    Args:
      binary_name: Name of the binary being verified, only used for error messages.
      lib_dir: The directory where the dependency libraries are supposed to be
          at runtime.
      deps: The list of DT_NEEDED dependency names for the current binary.
      get_lib_dependencies: A function that takes a runtime library path
          (e.g. "lib/libfoo.so") and returns the corresponding list of DT_NEEDED
          dependencies for its input, as a list of strings.
      visited_libraries: An optional set of file paths, which is updated
          by this function with the paths of the dependency libraries
          visited by this function.

    Returns:
        A list of error strings, which will be empty in case of success.
    """
    # Note that we do allow circular dependencies because they do happen
    # in practice. In particular when generating instrumented binaries,
    # e.g. for the 'asan' case (omitting libzircon.so):
    #
    #     libc.so (a.k.a. ld.so.1)
    #       ^     ^         ^ |
    #       |     |         | v
    #       |     |    libclang_rt.asan.so
    #       |     |     | ^      ^
    #       |     |     v |      |
    #       |    libc++abi.so    |
    #       |     |              |
    #       |     v              |
    #     libunwind.so-----------'
    #
    errors: List[str] = []
    queue: Set[str] = set(deps)
    while queue:
        dep = queue.pop()
        dep2 = _rewrite_elf_needed(dep)
        if dep2 is None:
            continue
        dep_path = os.path.join(lib_dir, dep2)
        if dep_path in visited_libraries:
            continue
        subdeps = get_lib_dependencies(dep_path)
        if subdeps is None:
            errors.append('%s missing dependency %s' % (binary_name, dep_path))
        else:
            visited_libraries.add(dep_path)
            for subdep in subdeps:
                if os.path.join(lib_dir, subdep) not in visited_libraries:
                    queue.add(subdep)

    return errors
