// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package elflib

import (
	"debug/pe"
	"encoding/binary"
	"fmt"
	"io"
)

// Format bits gleaned from LLVM source code:
// llvm/include/llvm/BinaryFormat/COFF.h (llvm::object::debug_directory)
// llvm/include/llvm/Object/CVDebugRecord.h (llvm::codeview::PDB70DebugInfo)

const (
	DebugDirectorySize          = 28
	DebugDirectoryTypeOffset    = 12
	DebugDirectorySizeOffset    = 16
	DebugDirectoryAddressOffset = 20
	IMAGE_DEBUG_TYPE_CODEVIEW   = 2
	PDB70                       = 0x53445352
)

func loadData(filename string, file io.ReaderAt, peFile *pe.File, dir pe.DataDirectory) ([]byte, error) {
	for _, scn := range peFile.Sections {
		if start := dir.VirtualAddress - scn.VirtualAddress; scn.VirtualAddress <= dir.VirtualAddress &&
			start < scn.VirtualSize &&
			scn.VirtualSize-dir.VirtualAddress >= dir.Size {
			end := start + dir.Size
			data, err := scn.Data()
			if err != nil {
				return nil, fmt.Errorf("error reading debug directory in %s: %w", filename, err)
			}
			return data[start:end], nil
		}
	}
	return nil, fmt.Errorf("debug virtual address not found in sections in %s", filename)
}

func parsePDBInfo(filename string, data []byte) ([]byte, error) {
	if len(data) < 24 {
		return nil, fmt.Errorf("debug directory too small (%d) in %s", len(data), filename)
	}
	if cvsig := binary.LittleEndian.Uint32(data[0:4]); cvsig != PDB70 {
		return nil, fmt.Errorf("unexpected debug directory CVSignature %x in %s", cvsig, filename)
	}

	// Following logic in lldb's GetCoffUUID, see:
	// lldb/source/Plugins/ObjectFile/PECOFF/ObjectFilePECOFF.cpp

	binary.LittleEndian.PutUint32(data[4:8], binary.BigEndian.Uint32(data[4:8]))
	binary.LittleEndian.PutUint16(data[8:10], binary.BigEndian.Uint16(data[8:10]))
	binary.LittleEndian.PutUint16(data[10:12], binary.BigEndian.Uint16(data[10:12]))
	if age := binary.LittleEndian.Uint32(data[20:24]); age != 0 {
		binary.LittleEndian.PutUint32(data[20:24], binary.BigEndian.Uint32(data[20:24]))
		return data[4:24], nil
	}
	return data[4:20], nil
}

func PEGetBuildIDs(filename string, file io.ReaderAt) ([][]byte, error) {
	peFile, err := pe.NewFile(file)
	if err != nil {
		return nil, fmt.Errorf("could not parse PE-COFF file %s: %w", filename, err)
	}
	hdr := peFile.OptionalHeader
	hdr64, ok := hdr.(*pe.OptionalHeader64)
	if !ok {
		return nil, fmt.Errorf("PE-COFF file %s not 64-bit?", filename)
	}
	dir := hdr64.DataDirectory[pe.IMAGE_DIRECTORY_ENTRY_DEBUG]
	data, err := loadData(filename, file, peFile, dir)
	if err != nil {
		return nil, err
	}
	if len(data)%DebugDirectorySize != 0 {
		return nil, fmt.Errorf("debug directory has in %s odd size %#x", filename, len(data))
	}
	out := [][]byte{}
	for pos := 0; pos < len(data); pos += DebugDirectorySize {
		tpos := pos + DebugDirectoryTypeOffset
		dtype := binary.LittleEndian.Uint32(data[tpos : tpos+4])
		if dtype == IMAGE_DEBUG_TYPE_CODEVIEW {
			apos := pos + DebugDirectoryAddressOffset
			spos := pos + DebugDirectorySizeOffset
			pdbaddr := binary.LittleEndian.Uint32(data[apos : apos+4])
			pdbsize := binary.LittleEndian.Uint32(data[spos : spos+4])
			pdbdata, err := loadData(filename, file, peFile, pe.DataDirectory{VirtualAddress: pdbaddr, Size: pdbsize})
			if err != nil {
				return nil, err
			}
			uuid, err := parsePDBInfo(filename, pdbdata)
			if err != nil {
				return nil, err
			}
			out = append(out, uuid)
		}
	}
	return out, nil
}
