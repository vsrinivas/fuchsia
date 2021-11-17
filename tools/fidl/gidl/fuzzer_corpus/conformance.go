// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzzer_corpus

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"path"

	gidlconfig "go.fuchsia.dev/fuchsia/tools/fidl/gidl/config"
	gidlir "go.fuchsia.dev/fuchsia/tools/fidl/gidl/ir"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type distributionEntry struct {
	Source      string `json:"source"`
	Destination string `json:"destination"`
}

type testCase struct {
	name        string
	objectTypes []fidlgen.ObjectType
	bytes       []byte
}

func getEncoding(encodings []gidlir.Encoding) (gidlir.Encoding, bool) {
	for _, encoding := range encodings {
		// Only supported encoding wire format: v1.
		if encoding.WireFormat == gidlir.V1WireFormat {
			return encoding, true
		}
	}

	return gidlir.Encoding{}, false
}

func getHandleDispositionEncoding(encodings []gidlir.HandleDispositionEncoding) (gidlir.HandleDispositionEncoding, bool) {
	for _, encoding := range encodings {
		// Only supported encoding wire format: v1.
		if encoding.WireFormat == gidlir.V1WireFormat {
			return encoding, true
		}
	}

	return gidlir.HandleDispositionEncoding{}, false
}

func getHandleObjectTypes(handles []gidlir.Handle, defs []gidlir.HandleDef) []fidlgen.ObjectType {
	var objectTypes []fidlgen.ObjectType
	for _, h := range handles {
		objectTypes = append(objectTypes, fidlgen.ObjectTypeFromHandleSubtype(defs[h].Subtype))
	}
	return objectTypes
}

func convertEncodeSuccesses(gtcs []gidlir.EncodeSuccess) []testCase {
	var tcs []testCase
	for _, gtc := range gtcs {
		encoding, ok := getHandleDispositionEncoding(gtc.Encodings)
		if !ok {
			continue
		}

		tcs = append(tcs, testCase{
			name:        fmt.Sprintf("EncodeSuccess_%s", gtc.Name),
			objectTypes: getHandleObjectTypes(gidlir.GetHandlesFromHandleDispositions(encoding.HandleDispositions), gtc.HandleDefs),
			bytes:       encoding.Bytes,
		})
	}

	return tcs
}

func convertDecodeSuccesses(gtcs []gidlir.DecodeSuccess) (tcs []testCase) {
	for _, gtc := range gtcs {
		encoding, ok := getEncoding(gtc.Encodings)
		if !ok {
			continue
		}

		tcs = append(tcs, testCase{
			name:        fmt.Sprintf("DecodeSuccess_%s", gtc.Name),
			objectTypes: getHandleObjectTypes(encoding.Handles, gtc.HandleDefs),
			bytes:       encoding.Bytes,
		})
	}

	return tcs
}

func convertDecodeFailures(gtcs []gidlir.DecodeFailure) (tcs []testCase) {
	for _, gtc := range gtcs {
		encoding, ok := getEncoding(gtc.Encodings)
		if !ok {
			continue
		}

		tcs = append(tcs, testCase{
			name:        fmt.Sprintf("DecodeFailure_%s", gtc.Name),
			objectTypes: getHandleObjectTypes(encoding.Handles, gtc.HandleDefs),
			bytes:       encoding.Bytes,
		})
	}

	return tcs
}

func getData(tc testCase) []byte {
	var buf bytes.Buffer

	// Put handle and message data at head of fuzzer input.
	for _, objectType := range tc.objectTypes {
		binary.Write(&buf, binary.LittleEndian, uint32(objectType))
	}
	buf.Write(tc.bytes)

	// Put length-encoding at the tail of fuzzer input.
	binary.Write(&buf, binary.LittleEndian, uint64(len(tc.objectTypes)))

	return buf.Bytes()
}

func writeTestCase(hostDir string, packageDataDir string, tc testCase) (distributionEntry, error) {
	data := getData(tc)

	filePath := path.Join(hostDir, tc.name)
	err := fidlgen.WriteFileIfChanged(filePath, data)
	if err != nil {
		return distributionEntry{}, err
	}

	return distributionEntry{
		Source:      filePath,
		Destination: path.Join("data", packageDataDir, tc.name),
	}, err
}

func GenerateConformanceTests(gidl gidlir.All, _ fidlgen.Root, config gidlconfig.GeneratorConfig) ([]byte, error) {
	if config.FuzzerCorpusHostDir == "" {
		return nil, errors.New("Must specify --fuzzer-corpus-host-dir when generating fuzzer_corpus")
	}
	if config.FuzzerCorpusPackageDataDir == "" {
		return nil, errors.New("Must specify --fuzzer-corpus-package-data-dir when generating fuzzer_corpus")
	}

	var manifest []distributionEntry

	for _, tcs := range [][]testCase{
		convertEncodeSuccesses(gidl.EncodeSuccess),
		convertDecodeSuccesses(gidl.DecodeSuccess),
		convertDecodeFailures(gidl.DecodeFailure),
	} {
		for _, tc := range tcs {
			entry, err := writeTestCase(config.FuzzerCorpusHostDir, config.FuzzerCorpusPackageDataDir, tc)
			if err != nil {
				return nil, err
			}
			manifest = append(manifest, entry)
		}
	}

	return json.Marshal(manifest)
}
