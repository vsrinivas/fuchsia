// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package far implements Fuchsia archive operations. At this time the optional
// hash chunks are not written, and only archive writing is supported. The
// specification for the archive format can be found in
// https://fuchsia.googlesource.com/docs/+/master/archive_format.md.
package far

import (
	"encoding/binary"
	"io"
	"os"
	"sort"
)

// Magic is the first bytes of a FAR archive
const Magic = "\xc8\xbf\x0b\x48\xad\xab\xc5\x11"

// ChunkType is a uint64 representing the type of a non-index chunk
type ChunkType uint64

// alignment values from the FAR specification
const (
	contentAlignment = 4096
	nameAlignment    = 8
)

// Various chunk types
const (
	HashChunk     ChunkType = 0                  // 0
	DirHashChunk  ChunkType = 0x2d48534148524944 // "DIRHASH-"
	DirChunk      ChunkType = 0x2d2d2d2d2d524944 // "DIR-----"
	DirNamesChunk ChunkType = 0x53454d414e524944 // "DIRNAMES"
)

// Index is the first chunk of an archive
type Index struct {
	// Magic bytes must equal the Magic constant
	Magic [8]byte
	// Length of all index entries in bytes
	Length uint64
}

// IndexLen is the byte size of the Index struct
const IndexLen = 8 + 8

// IndexEntry identifies the type and position of a chunk in the archive
type IndexEntry struct {
	// Type of the chunk
	Type ChunkType
	// Offset from the start of the archive
	Offset uint64
	// Length of the chunk
	Length uint64
}

// IndexEntryLen is the byte size of the IndexEntry struct
const IndexEntryLen = 8 + 8 + 8

// DirectoryEntry indexes into the dirnames and contents chunks to provide
// access to file names and file contents respectively.
type DirectoryEntry struct {
	NameOffset uint32
	NameLength uint16
	Reserved   uint16
	DataOffset uint64
	DataLength uint64
	Reserved2  uint64
}

// DirectoryEntryLen is the byte size of the DirectoryEntry struct
const DirectoryEntryLen = 4 + 2 + 2 + 8 + 8 + 8

// PathData is a concatenated list of names of files that makes up the unpadded
// portion of a dirnames chunk.
type PathData []byte

// Write writes a list of files to the given io. The inputs map provides a list
// of target archive paths mapped to on-disk file paths from which the content
// should be fetched.
func Write(w io.Writer, inputs map[string]string) error {
	var filenames = make([]string, 0, len(inputs))
	for name := range inputs {
		filenames = append(filenames, name)
	}
	sort.Strings(filenames)

	var pathData PathData
	var entries []DirectoryEntry
	for _, name := range filenames {
		bname := []byte(name)
		entries = append(entries, DirectoryEntry{
			NameOffset: uint32(len(pathData)),
			NameLength: uint16(len(bname)),
		})
		pathData = append(pathData, bname...)
	}

	index := Index{
		Length: IndexEntryLen * 2,
	}
	copy(index.Magic[:], []byte(Magic))

	dirIndex := IndexEntry{
		Type:   DirChunk,
		Offset: IndexLen + IndexEntryLen*2,
		Length: uint64(len(entries) * DirectoryEntryLen),
	}

	nameIndex := IndexEntry{
		Type:   DirNamesChunk,
		Offset: dirIndex.Offset + dirIndex.Length,
		Length: align(uint64(len(pathData)), 8),
	}

	if err := binary.Write(w, binary.LittleEndian, index); err != nil {
		return err
	}
	if err := binary.Write(w, binary.LittleEndian, dirIndex); err != nil {
		return err
	}
	if err := binary.Write(w, binary.LittleEndian, nameIndex); err != nil {
		return err
	}

	contentOffset := align(nameIndex.Offset+nameIndex.Length, contentAlignment)

	for i := range entries {
		entries[i].DataOffset = contentOffset
		n, err := fileSize(inputs[filenames[i]])
		if err != nil {
			return err
		}
		entries[i].DataLength = uint64(n)
		contentOffset = align(contentOffset+entries[i].DataLength, contentAlignment)

		if err := binary.Write(w, binary.LittleEndian, entries[i]); err != nil {
			return err
		}
	}

	if err := binary.Write(w, binary.LittleEndian, pathData); err != nil {
		return err
	}

	if _, err := w.Write(make([]byte, int(nameIndex.Length)-len(pathData))); err != nil {
		return err
	}

	pos := nameIndex.Offset + nameIndex.Length
	pad := align(pos, contentAlignment) - pos
	if _, err := w.Write(make([]byte, pad)); err != nil {
		return err
	}

	for i, name := range filenames {
		f, err := os.Open(inputs[name])
		if err != nil {
			return err
		}
		if _, err := io.Copy(w, f); err != nil {
			return err
		}
		if err := f.Close(); err != nil {
			return err
		}

		pos := entries[i].DataOffset + entries[i].DataLength
		pad := align(pos, contentAlignment) - pos
		if _, err := w.Write(make([]byte, pad)); err != nil {
			return err
		}
	}

	return nil
}

// align rounds i up to a multiple of n
func align(i, n uint64) uint64 {
	n--
	return (i + n) & ^n
}

func fileSize(path string) (int64, error) {
	info, err := os.Stat(path)
	if err != nil {
		return 0, err
	}
	return info.Size(), nil
}
