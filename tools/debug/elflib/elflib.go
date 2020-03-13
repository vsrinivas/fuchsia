// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package elflib provides methods for handling ELF files.
package elflib

import (
	"bufio"
	"bytes"
	"debug/elf"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
)

const NT_GNU_BUILD_ID uint32 = 3

// DebugFileSuffix is the file suffix used by unstripped debug binaries.
const DebugFileSuffix = ".debug"

// BinaryFileRef represents a reference to an ELF file. The build id
// and filepath are stored here. BinaryFileRefs can verify that their
// build id matches their contents.
type BinaryFileRef struct {
	BuildID  string
	Filepath string
}

// NewBinaryFileRef returns a BinaryFileRef with the given build id and filepath.
func NewBinaryFileRef(filepath, build string) BinaryFileRef {
	return BinaryFileRef{BuildID: build, Filepath: filepath}
}

type buildIDError struct {
	err      error
	filename string
}

func newBuildIDError(err error, filename string) *buildIDError {
	return &buildIDError{err: err, filename: filename}
}

func (b buildIDError) Error() string {
	return fmt.Sprintf("error reading %s: %v", b.filename, b.err)
}

// Verify verifies that the build id of b matches the build id found in the file.
func (b BinaryFileRef) Verify() error {
	file, err := os.Open(b.Filepath)
	if err != nil {
		return newBuildIDError(err, b.Filepath)
	}
	defer file.Close()
	buildIDs, err := GetBuildIDs(b.Filepath, file)
	if err != nil {
		return newBuildIDError(err, b.Filepath)
	}
	binBuild, err := hex.DecodeString(b.BuildID)
	if err != nil {
		return newBuildIDError(fmt.Errorf("build ID `%s` is not a hex string: %w", b.BuildID, err), b.Filepath)
	}
	for _, buildID := range buildIDs {
		if bytes.Equal(buildID, binBuild) {
			return nil
		}
	}
	return newBuildIDError(fmt.Errorf("build ID `%s` could not be found", b.BuildID), b.Filepath)
}

// HasDebugInfo checks if file contains debug_info section.
func (b BinaryFileRef) HasDebugInfo() (bool, error) {
	elfFile, err := elf.Open(b.Filepath)
	if err != nil {
		return false, err
	}
	defer elfFile.Close()
	for _, section := range elfFile.Sections {
		if section != nil && section.Name == ".debug_info" {
			return true, nil
		}
	}
	return false, nil
}

// rounds 'x' up to the next 'to' aligned value
func alignTo(x, to uint32) uint32 {
	return (x + to - 1) & -to
}

type noteEntry struct {
	noteType uint32
	name     string
	desc     []byte
}

func forEachNote(note []byte, endian binary.ByteOrder, entryFn func(noteEntry)) error {
	for {
		var out noteEntry
		// If there isn't enough to parse set n to nil.
		if len(note) < 12 {
			return nil
		}
		namesz := endian.Uint32(note[0:4])
		descsz := endian.Uint32(note[4:8])
		out.noteType = endian.Uint32(note[8:12])
		if namesz+12 > uint32(len(note)) {
			return fmt.Errorf("invalid name length in note entry")
		}
		out.name = string(note[12 : 12+namesz])
		// We need to account for padding at the end.
		descoff := alignTo(12+namesz, 4)
		if descoff+descsz > uint32(len(note)) {
			return fmt.Errorf("invalid desc length in note entry")
		}
		out.desc = note[descoff : descoff+descsz]
		next := alignTo(descoff+descsz, 4)
		// If the final padding isn't in the entry, don't throw error.
		if next >= uint32(len(note)) {
			note = note[len(note):]
		} else {
			note = note[next:]
		}
		entryFn(out)
	}
}

func getBuildIDs(filename string, endian binary.ByteOrder, data io.ReaderAt, size uint64) ([][]byte, error) {
	noteBytes := make([]byte, size)
	_, err := data.ReadAt(noteBytes, 0)
	if err != nil {
		return nil, fmt.Errorf("error parsing section header in %s: %w", filename, err)
	}
	out := [][]byte{}
	err = forEachNote(noteBytes, endian, func(entry noteEntry) {
		if entry.noteType != NT_GNU_BUILD_ID || entry.name != "GNU\000" {
			return
		}
		out = append(out, entry.desc)
	})
	return out, err
}

// GetBuildIDs parses and returns all the build ids from file's section/program headers.
func GetBuildIDs(filename string, file io.ReaderAt) ([][]byte, error) {
	elfFile, err := elf.NewFile(file)
	if err != nil {
		return nil, fmt.Errorf("could not parse ELF file %s: %w", filename, err)
	}
	if len(elfFile.Progs) == 0 && len(elfFile.Sections) == 0 {
		return nil, fmt.Errorf("no program headers or sections in %s", filename)
	}
	var endian binary.ByteOrder
	if elfFile.Data == elf.ELFDATA2LSB {
		endian = binary.LittleEndian
	} else {
		endian = binary.BigEndian
	}
	out := [][]byte{}
	// Check every SHT_NOTE section.
	for _, section := range elfFile.Sections {
		if section == nil || section.Type != elf.SHT_NOTE {
			continue
		}
		buildIDs, err := getBuildIDs(filename, endian, section, section.Size)
		if err != nil {
			return out, err
		}
		out = append(out, buildIDs...)
	}
	// If we found what we were looking for with sections, don't reparse the program
	// headers.
	if len(out) > 0 {
		return out, nil
	}
	// Check every PT_NOTE segment.
	for _, prog := range elfFile.Progs {
		if prog == nil || prog.Type != elf.PT_NOTE {
			continue
		}
		buildIDs, err := getBuildIDs(filename, endian, prog, prog.Filesz)
		if err != nil {
			return out, err
		}
		out = append(out, buildIDs...)
	}
	return out, nil
}

// GetSoName returns the soname of the ELF file.
func GetSoName(filename string, file io.ReaderAt) (string, error) {
	elfFile, err := elf.NewFile(file)
	if err != nil {
		return "", fmt.Errorf("could not parse ELF file %s: %w", filename, err)
	}
	strings, err := elfFile.DynString(elf.DT_SONAME)
	if err != nil {
		return "", fmt.Errorf("when parsing .dynamic from %s: %w", filename, err)
	}
	if len(strings) > 1 {
		return "", fmt.Errorf("expected at most one DT_SONAME entry in %s", filename)
	}
	if len(strings) == 0 {
		return "", nil
	}
	return strings[0], nil
}

// ReadIDsFile reads a list of build ids and corresponding filenames from an ids file
// and returns a list of BinaryFileRefs.
func ReadIDsFile(file io.Reader) ([]BinaryFileRef, error) {
	scanner := bufio.NewScanner(file)
	out := []BinaryFileRef{}
	for line := 1; scanner.Scan(); line++ {
		text := scanner.Text()
		parts := strings.SplitN(text, " ", 2)
		if len(parts) != 2 {
			return nil, fmt.Errorf("error parsing on line %d: found `%s`", line, text)
		}
		build := strings.TrimSpace(parts[0])
		filename := strings.TrimSpace(parts[1])
		out = append(out, BinaryFileRef{Filepath: filename, BuildID: build})
	}
	return out, nil
}

// WalkBuildIDDir walks the directory containing symbol files at dirpath and creates a
// BinaryFileRef for each symbol file it finds. Files without a ".debug" suffix are
// skipped. Each output BinaryFileRef's BuildID is formed by concatenating the file's
// basename with its parent directory's basename. For example:
//
// Input directory:
//
//   de/
//     adbeef.debug
//     admeat.debug
//
// Output BuildIDs:
//
//   deadbeef.debug
//   deadmeat.debug
func WalkBuildIDDir(dirpath string) ([]BinaryFileRef, error) {
	info, err := os.Stat(dirpath)
	if err != nil {
		return nil, fmt.Errorf("failed to stat %q: %w", dirpath, err)
	}
	if !info.IsDir() {
		return nil, fmt.Errorf("%q is not a directory", dirpath)
	}
	var refs []BinaryFileRef
	if err := filepath.Walk(dirpath, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return fmt.Errorf("%q: %w", path, err)
		}
		if info.IsDir() {
			return nil
		}
		if !strings.HasSuffix(path, DebugFileSuffix) {
			return nil
		}
		dir, basename := filepath.Split(path)
		buildID := filepath.Base(dir) + strings.TrimSuffix(basename, DebugFileSuffix)
		refs = append(refs, BinaryFileRef{
			Filepath: path,
			BuildID:  buildID,
		})
		return nil
	}); err != nil {
		return nil, err
	}
	return refs, nil
}
