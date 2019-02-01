// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"bufio"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"sort"
	"strings"
)

// MerkleRoot is the root hash of a merkle tree
type MerkleRoot [32]byte

// ErrInvalidMerkleRootLength indicates a MerkleRoot string did not contain 32 hex bytes
var ErrInvalidMerkleRootLength = errors.New("decoded merkle root does not contain 32 bytes")

// ErrInvalidMetaContentsLine indicates that a line in a meta/contents file was not valid
type ErrInvalidMetaContentsLine struct {
	Line string
}

func (e ErrInvalidMetaContentsLine) Error() string {
	return fmt.Sprintf("invalid line: %s", e.Line)
}

// MustDecodeMerkleRoot parses a MerkleRoot from a string, or panics
func MustDecodeMerkleRoot(s string) MerkleRoot {
	m, err := DecodeMerkleRoot([]byte(s))
	if err != nil {
		panic(err)
	}
	return m
}

// DecodeMerkleRoot attempts to parse a MerkleRoot from a string
func DecodeMerkleRoot(text []byte) (MerkleRoot, error) {
	var m MerkleRoot
	if err := m.UnmarshalText(text); err != nil {
		return m, err
	}
	return m, nil
}

// String encodes a MerkleRoot as a lower case 32 byte hex string
func (m MerkleRoot) String() string {
	return hex.EncodeToString(m[:])
}

// MarshalText implements encoding.TextMarshaler
func (m MerkleRoot) MarshalText() ([]byte, error) {
	return []byte(m.String()), nil
}

// UnmarshalText implements encoding.TextUnmarshaler
func (m *MerkleRoot) UnmarshalText(text []byte) error {
	n, err := hex.Decode(m[:], text)
	if err != nil {
		return err
	} else if n != 32 {
		return ErrInvalidMerkleRootLength
	}
	return nil
}

// LessThan provides a sort ordering for MerkleRoot
func (m MerkleRoot) LessThan(other MerkleRoot) bool {
	for i := 0; i < len(m); i++ {
		if m[i] < other[i] {
			return true
		} else if m[i] > other[i] {
			return false
		}
	}
	return false
}

// MetaContents maps file paths within a package to their content IDs
type MetaContents map[string]MerkleRoot

// LoadMetaContents attempts to parse a meta/contents file from disk
func LoadMetaContents(path string) (MetaContents, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	return ParseMetaContents(f)
}

// ParseMetaContents attempts to parse a meta/contents file from an io.Reader
func ParseMetaContents(r io.Reader) (MetaContents, error) {
	res := make(map[string]MerkleRoot)
	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		line := scanner.Text()

		parts := strings.SplitN(line, "=", 2)
		if len(parts) < 2 {
			return nil, ErrInvalidMetaContentsLine{line}
		}

		key := strings.TrimSpace(parts[0])
		var value MerkleRoot
		if err := value.UnmarshalText([]byte(strings.TrimSpace(parts[1]))); err != nil {
			return nil, err
		}

		res[key] = value
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return res, nil
}

// String serializes the instance in the manifest file format, which could be
// parsed by ParseMetaContents.
func (m MetaContents) String() string {
	contentLines := make([]string, 0, len(m))
	for path, root := range m {
		line := fmt.Sprintf("%s=%s\n", path, root)
		contentLines = append(contentLines, line)
	}
	sort.Strings(contentLines)

	return strings.Join(contentLines, "")
}
