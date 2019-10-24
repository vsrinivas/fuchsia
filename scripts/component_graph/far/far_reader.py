#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Far Reader allows converting far files python dictionaries.

This package provides a simple way to convert binary based Far files into python
dictionaries
which can converted into json for data analysis.
"""

import io
import json
import struct
from collections import namedtuple

FAR_MAGIC = b"\xc8\xbf\x0b\x48\xad\xab\xc5\x11"
FAR_DIR_CHUNK = int.from_bytes(b"DIR-----", byteorder="little")
FAR_DIR_NAMES_CHUNK = int.from_bytes(b"DIRNAMES", byteorder='little')
FAR_INDEX_FMT = "<8sQ"
FAR_INDEX_LEN = struct.calcsize(FAR_INDEX_FMT)
FAR_INDEX_ENTRY_FMT = "<QQQ"
FAR_INDEX_ENTRY_LEN = struct.calcsize(FAR_INDEX_ENTRY_FMT)
FAR_DIR_ENTRY_FMT = "<IHHQQQ"
FAR_DIR_ENTRY_LEN = struct.calcsize(FAR_DIR_ENTRY_FMT)

FarIndex = namedtuple("Index", "magic length")
FarIndexEntry = namedtuple("IndexEntry", "chunk_type offset length")
FarDirectoryEntry = namedtuple(
    "DirectoryEntry",
    "name_offset name_length reserved data_offset data_length reserved2"
)

class FarFormatError(Exception):
  """Exception raised from unexpected errors in the far format when being parssed.
  Attributes:
    message - explanation of the error
  """
  pass

def far_read_index(byte_stream):
    """Unpacks the Index structure at the head of all valid FAR files and validates contents."""
    far_index_bytes = byte_stream.read(FAR_INDEX_LEN)
    if len(far_index_bytes) != FAR_INDEX_LEN:
      raise FarFormatError("Unexpected EOF parsing far index bytes.")

    index = FarIndex._make(
        struct.unpack(FAR_INDEX_FMT, far_index_bytes))
    if index.magic != FAR_MAGIC:
      raise FarFormatError("Expected magic number does not match.")
    if index.length % FAR_INDEX_ENTRY_LEN != 0:
      raise FarFormatError("Index length isn't aligned to far index entry length.")
    return index

def far_read_index_entry(byte_stream):
    """ Unpacks an IndexEntry that defines the offset of directory entries. """
    return FarIndexEntry._make(
        struct.unpack(
            FAR_INDEX_ENTRY_FMT, byte_stream.read(FAR_INDEX_ENTRY_LEN)))

def far_read_directory_entry(byte_stream):
    """ Unpacks a directory entry which holds its name and file offset. """
    far_dir_entry_bytes = byte_stream.read(FAR_DIR_ENTRY_LEN)
    if len(far_dir_entry_bytes) != FAR_DIR_ENTRY_LEN:
      raise FarFormatError("Unexpected EOF parsing FAR directory entry")

    return FarDirectoryEntry._make(
        struct.unpack(
            FAR_DIR_ENTRY_FMT,
            far_dir_entry_bytes))

def far_read(far_buffer):
    """Reads the contents of the far file returning all files in a dictionary with their data."""
    # Verify the file is valid and the index page is not corrupted.
    stream = io.BytesIO(far_buffer)
    index = far_read_index(stream)
    if index.length == 0:
      raise FarFormatError("Empty archive.")
    # Parse the directory chunk and names chunk from the index page.
    dir_index = None
    dir_name_index = None
    for i in range(0, index.length // FAR_INDEX_ENTRY_LEN):
        entry = far_read_index_entry(stream)
        if entry.chunk_type == FAR_DIR_CHUNK:
            dir_index = entry
        elif entry.chunk_type == FAR_DIR_NAMES_CHUNK:
            dir_name_index = entry
            if dir_index == None:
              raise FarFormatError("Misordered index entries.")
        else:
          raise FarFormatError("Unexpected chunk type.")

    if dir_index == None:
      raise FarFormatError("Unable to find the directory index.")
    if dir_name_index == None:
      raise FarFormatError("Unable to find the directory name index.")

    stream.seek(dir_name_index.offset)
    path_data = stream.read(dir_name_index.length)
    if len(path_data) != dir_name_index.length:
      raise FarFormatError("Encountered EOF parsing directory path.")

    # Parse the files.
    file_entries = []
    stream.seek(dir_index.offset)
    for i in range(0, dir_index.length // FAR_DIR_ENTRY_LEN):
        entry = far_read_directory_entry(stream)
        name = path_data[entry.name_offset:entry.name_offset +
                         entry.name_length]
        file_entries.append((name, entry))

    export = {}
    for path, entry in file_entries:
        stream.seek(entry.data_offset)
        entry_bytes = stream.read(entry.data_length)
        if len(entry_bytes) != entry.data_length:
          raise FarFormatError("Encountered unexpected EOF while parsing entry.")
        export[path.decode()] = entry_bytes
    return export

# TODO(benwright) - Move to a different file when the component_graph lands.
def read_package(far_buffer):
    """Performs a raw_read then intelligently restructures known package structures."""
    files = far_read(far_buffer)

    if "meta/contents" in files:
      content = files["meta/contents"].decode()
      files["meta/contents"] = dict(
            [tuple(e.rsplit("=", maxsplit=1)) for e in content.split("\n") if e])
    if "meta/package" in files:
      files["meta/package"] = json.loads(files["meta/package"].decode())
    json_extensions = [".cm", ".cmx"]
    for ext in json_extensions:
        for path in files.keys():
            if path.endswith(ext):
                files[path] = json.loads(files[path])
    return files

# Utility to allow using
if __name__ == "__main__":
  import sys
  from pprint import pprint
  if len(sys.argv) != 2:
      print("Please provide a file to parse.")
      sys.exit(1)
  try:
    pprint(read_package(open(sys.argv[1], "rb").read()))
  except FarFormatError as e:
    print("Failed to parse the file error: ", e)

