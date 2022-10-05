// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package pprof

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"runtime/pprof"
	"sort"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/inspect"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/inspect/vmobuffer"
	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"go.uber.org/multierr"
)

const pprofName = "pprof"

func Setup(path string) (component.Node, func() error, error) {
	mapDir, err := mapDirFromPath(path)
	if err != nil {
		return nil, nil, err
	}

	return &component.DirectoryWrapper{
			Directory: mapDir,
		}, func() error {
			t := time.NewTicker(time.Minute)
			defer t.Stop()

			for {
				if err := func() error {
					mapDir.mu.Lock()
					defer mapDir.mu.Unlock()
					// Prune all but the most recent profiles. The number retained is
					// chosen arbitrarily.
					if maxProfiles := 3; len(mapDir.mu.m) > maxProfiles {
						filenames := make([]string, 0, len(mapDir.mu.m))
						for filename := range mapDir.mu.m {
							filenames = append(filenames, filename)
						}
						sort.Strings(filenames)
						for _, filename := range filenames[:len(filenames)-maxProfiles] {
							delete(mapDir.mu.m, filename)
							if err := os.Remove(filepath.Join(path, filename)); err != nil {
								_ = syslog.Warnf("failed to remove %s: %s", filename, err)
							}
						}
					}
					return nil
				}(); err != nil {
					return err
				}

				filename := (<-t.C).UTC().Format(time.RFC3339) + ".inspect"

				profilePath := filepath.Join(path, filename)

				if err := writeProfilesInspectVMOBytes(profilePath); err != nil {
					return err
				}

				mapDir.mu.Lock()
				mapDir.mu.m[filename] = &component.FileWrapper{
					File: newStdioFile(profilePath),
				}
				mapDir.mu.Unlock()
			}
		}, nil
}

var _ component.Directory = (*mapDirectory)(nil)

type mapDirectory struct {
	mu struct {
		sync.Mutex
		m map[string]*component.FileWrapper
	}
}

func mapDirFromPath(path string) (*mapDirectory, error) {
	dir, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	filenames, err := dir.Readdirnames(0)
	if err != nil {
		return nil, err
	}
	if err := dir.Close(); err != nil {
		return nil, err
	}

	m := make(map[string]*component.FileWrapper)
	for _, filename := range filenames {
		path := filepath.Join(path, filename)
		m[filename] = &component.FileWrapper{
			File: newStdioFile(path),
		}
	}
	var d mapDirectory
	d.mu.m = m
	return &d, nil
}

const nowName = "now.inspect"

var nowFile = component.FileWrapper{File: &pprofFile{}}

func (md *mapDirectory) Get(nodeName string) (component.Node, bool) {
	if nodeName == nowName {
		return &nowFile, true
	}
	md.mu.Lock()
	value, ok := md.mu.m[nodeName]
	md.mu.Unlock()
	return value, ok
}

func (md *mapDirectory) ForEach(fn func(string, component.Node) error) error {
	if err := fn(nowName, &nowFile); err != nil {
		return err
	}
	md.mu.Lock()
	defer md.mu.Unlock()
	for nodeName, node := range md.mu.m {
		if err := fn(nodeName, node); err != nil {
			return err
		}
	}
	return nil
}

var _ component.File = (*pprofFile)(nil)

type pprofFile struct{}

func writeProfilesInspectVMOBytes(path string) error {
	file, err := os.OpenFile(path, os.O_RDWR|os.O_CREATE, os.ModePerm)
	if err != nil {
		return fmt.Errorf("os.Create(%q) = %w", path, err)
	}
	defer file.Close()

	bufferedWriter := bufio.NewWriter(file)
	{
		err := writeProfilesInspectVMOBytesWriter(bufferedWriter)
		return multierr.Append(err, bufferedWriter.Flush())
	}
}

func writeProfilesInspectVMOBytesWriter(writer io.Writer) error {
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

	if err := writeProfilesInspectVMOBytesWriter(b); err != nil {
		return nil, 0, err
	}

	r, err := b.ToVMOReader()
	if err != nil {
		return nil, 0, err
	}

	return r, r.Size(), nil
}

var _ component.File = (*stdioFile)(nil)

// stdioFile provides a component.File implementation based on a stdio File.
type stdioFile struct {
	path string
}

func newStdioFile(path string) *stdioFile {
	return &stdioFile{path}
}

func (f *stdioFile) GetReader() (component.Reader, uint64, error) {
	fileInfo, err := os.Stat(f.path)
	if err != nil {
		return nil, 0, err
	}

	file, err := os.Open(f.path)
	if err != nil {
		return nil, 0, err
	}

	return component.NoVMO(file), uint64(fileInfo.Size()), nil
}
