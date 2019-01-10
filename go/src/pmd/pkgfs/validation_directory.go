// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"sort"
	"thinfs/fs"
	"time"
)

// validationDir has two entries, present and missing which represent the blobs
// from the static index which are in and not in (respectively) blobfs. This
// uses Filesystem.ValidateStaticIndex() to determine what is or is not there.
type validationDir struct {
	unsupportedDirectory
	fs *Filesystem
}

func (valDir *validationDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, valDir, nil, nil
	}

	// we only serve two files
	if name != "present" && name != "missing" {
		return nil, nil, nil, fs.ErrNotFound
	}

	// we're read only
	if flags.Write() || flags.Truncate() || flags.Directory() || flags.Append() {
		return nil, nil, nil, fs.ErrNotSupported
	}

	present, missing, err := valDir.fs.ValidateStaticIndex()
	if err != nil {
		return nil, nil, nil, fs.ErrFailedPrecondition
	}

	pEnts := make([]string, 0, len(present))
	for entry := range present {
		pEnts = append(pEnts, entry)
	}

	mEnts := make([]string, 0, len(missing))
	for entry := range missing {
		mEnts = append(mEnts, entry)
	}

	sort.Strings(pEnts)
	sort.Strings(mEnts)

	t := time.Now()
	switch name {
	case "present":
		return &validationFile{unsupportedFile: unsupportedFile("present"), entries: pEnts, statTime: t}, nil, nil, nil
	case "missing":
		return &validationFile{unsupportedFile: unsupportedFile("missing"), entries: mEnts, statTime: t}, nil, nil, nil
	default:
		// should actually be impossible to get here given the check above
		return nil, nil, nil, fs.ErrNotFound
	}
}

func (valDir *validationDir) Read() ([]fs.Dirent, error) {
	return []fs.Dirent{
		fileDirEnt("missing"),
		fileDirEnt("present"),
	}, nil
}

func (valDir *validationDir) Stat() (int64, time.Time, time.Time, error) {
	t := time.Now()
	return 2, t, t, nil
}

const lineSize = int64(64 + 1)

// validationFile represents the present or missing blobs in the static index.
// The size of the file as reported by Stat() indicates the number of present
// or missing blobs
type validationFile struct {
	unsupportedFile
	entries  []string
	statTime time.Time
	seek     int64
}

func (valFile *validationFile) Stat() (int64, time.Time, time.Time, error) {
	return int64(len(valFile.entries)) * lineSize, valFile.statTime, valFile.statTime, nil
}

// Read reads a maximum of len(p) bytes into "p" from the file at a location decided by "off"
// and "whence".
// The seek position is only updated if fs.WhenceFromCurrent is passed as whence.
func (valFile *validationFile) Read(p []byte, off int64, whence int) (int, error) {
	if whence == fs.WhenceFromCurrent {
		valFile.seek += off
	} else {
		return 0, fs.ErrInvalidArgs
	}

	if valFile.seek < 0 {
		valFile.seek = 0
	}

	listOffset := valFile.seek / lineSize
	entPos := valFile.seek % lineSize

	copied := 0
	for ; copied < len(p); listOffset++ {
		if listOffset >= int64(len(valFile.entries)) {
			if copied < len(p) {
				return copied, fs.ErrEOF
			}
			break
		}

		src := []byte(valFile.entries[listOffset][entPos:])
		entPos = 0
		moved := copy(p[copied:], src)
		copied += moved
		valFile.seek += int64(moved)

		if len(p) > copied {
			p[copied] = '\n'
			copied++
			valFile.seek++
		}

		if copied == len(p) {
			return len(p), nil
		}
	}

	return copied, nil
}

func (valFile *validationFile) Close() error {
	valFile.seek = 0
	return nil
}
