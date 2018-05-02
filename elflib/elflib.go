// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package elflib

import (
	"debug/elf"
	"encoding/binary"
	"fmt"
	"io"
)

const NT_GNU_BUILD_ID uint32 = 3

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

func GetBuildIDs(filename string, file io.ReaderAt) ([][]byte, error) {
	elfFile, err := elf.NewFile(file)
	if err != nil {
		return nil, fmt.Errorf("could not parse ELF file %s: %v", filename, err)
	}
	if len(elfFile.Progs) == 0 {
		return nil, fmt.Errorf("no program headers in %s", filename)
	}
	var endian binary.ByteOrder
	if elfFile.Data == elf.ELFDATA2LSB {
		endian = binary.LittleEndian
	} else {
		endian = binary.BigEndian
	}
	out := [][]byte{}
	// Check every PT_NOTE segment.
	for _, prog := range elfFile.Progs {
		if prog == nil || prog.Type != elf.PT_NOTE {
			continue
		}
		noteBytes := make([]byte, prog.Filesz)
		_, err := prog.ReadAt(noteBytes, 0)
		if err != nil {
			return nil, fmt.Errorf("error parsing program header in %s: %v", filename, err)
		}
		// While the part of the note segment we're looking at doesn't have more valid data.
		err = forEachNote(noteBytes, endian, func(entry noteEntry) {
			if entry.noteType != NT_GNU_BUILD_ID || entry.name != "GNU\000" {
				return
			}
			out = append(out, entry.desc)
		})
		if err != nil {
			return out, err
		}
	}
	return out, nil
}
