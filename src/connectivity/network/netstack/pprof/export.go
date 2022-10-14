// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package pprof

import (
	"bytes"
	"io"
	"runtime/pprof"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/inspect"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/inspect/vmobuffer"
	"go.fuchsia.dev/fuchsia/src/lib/component"
)

const pprofName = "pprof"

func NewNode() component.Node {
	return &component.DirectoryWrapper{Directory: &mapDirectory{}}
}

var _ component.Directory = (*mapDirectory)(nil)

type mapDirectory struct{}

const nowName = "now.inspect"

var nowFile = component.FileWrapper{File: &pprofFile{}}

func (md *mapDirectory) Get(nodeName string) (component.Node, bool) {
	if nodeName == nowName {
		return &nowFile, true
	}
	return nil, false
}

func (md *mapDirectory) ForEach(fn func(string, component.Node) error) error {
	if err := fn(nowName, &nowFile); err != nil {
		return err
	}
	return nil
}

var _ component.File = (*pprofFile)(nil)

type pprofFile struct{}

func writeProfilesInspectVMOBytes(writer io.Writer) error {
	w, err := inspect.NewWriter(writer)
	if err != nil {
		return err
	}
	nodeValueIndex, err := w.WriteNodeValueBlock(0, pprofName)
	if err != nil {
		return err
	}
	var buffer bytes.Buffer
	for _, p := range pprof.Profiles() {
		buffer.Reset()
		if err := p.WriteTo(&buffer, 0); err != nil {
			return err
		}
		if err := w.WriteBinary(nodeValueIndex, p.Name(), uint32(buffer.Len()), &buffer); err != nil {
			return err
		}
	}
	return nil
}

// Chosen arbitrarily.
const vmoBufferSizeBytes uint64 = 4096

func (p *pprofFile) GetReader() (component.Reader, uint64, error) {
	b, err := vmobuffer.NewVMOBuffer(vmoBufferSizeBytes, "pprof-VMOBuffer")
	if err != nil {
		return nil, 0, err
	}
	defer b.Close()

	if err := writeProfilesInspectVMOBytes(b); err != nil {
		return nil, 0, err
	}

	r, err := b.ToVMOReader()
	if err != nil {
		return nil, 0, err
	}

	return r, r.Size(), nil
}
