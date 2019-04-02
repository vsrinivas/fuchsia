// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package breakpad

import (
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"strings"
)

// ParseSymbolFile reads a breakpad symbol file.
func ParseSymbolFile(r io.Reader) (*SymbolFile, error) {
	bytes, err := ioutil.ReadAll(r)
	if err != nil {
		return nil, fmt.Errorf("failed to read symbol file: %v", err)
	}
	lines := strings.SplitN(string(bytes), "\n", 2)
	if len(lines) != 2 {
		return nil, fmt.Errorf("got < 2 lines in symbol file")
	}
	moduleSection := strings.TrimSpace(lines[0])
	if moduleSection == "" {
		return nil, fmt.Errorf("unexpected blank first line in symbol file")
	}
	module, err := parseModuleLine(moduleSection)
	if err != nil {
		return nil, fmt.Errorf("failed to parse module section: %v", err)
	}
	return &SymbolFile{
		ModuleSection: module,
		remainder:     lines[1],
	}, nil
}

// SymbolFile represents a Breakpad symbol file, typically produced by the dump_syms tool.
// This is only a partial implementation; the ModuleSection is all that is parsed. Add fields
// as needed.
type SymbolFile struct {
	ModuleSection *ModuleSection

	// Unparsed contents from the input file excluding the ModuleSection.
	remainder string
}

func (f SymbolFile) WriteTo(w io.Writer) (int64, error) {
	lines := append([]string{f.ModuleSection.String()}, f.remainder)
	content := strings.Join(lines, "\n")
	return io.Copy(w, bytes.NewReader([]byte(content)))
}

// ModuleSection represents the first section/line of a symbol file.
type ModuleSection struct {
	OS         string
	Arch       string
	BuildID    string
	ModuleName string
}

// String formats this ModuleSection as it would be written in a symbol file.
func (mod *ModuleSection) String() string {
	content := fmt.Sprintf("MODULE %s %s %s %s", mod.OS, mod.Arch, mod.BuildID, mod.ModuleName)
	return strings.TrimSpace(content)
}

func parseModuleLine(source string) (*ModuleSection, error) {
	format := "MODULE $OS $ARCH $BUILD_ID $MODULE_NAME"
	parts := strings.Split(source, " ")
	if len(parts) != 5 {
		return nil, fmt.Errorf("wanted line 1 with format %q but got %q", format, source)
	}
	return &ModuleSection{
		OS:         parts[1],
		Arch:       parts[2],
		BuildID:    parts[3],
		ModuleName: parts[4],
	}, nil
}
