// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"flag"
	"fmt"
	"path/filepath"
	"runtime"
	"testing"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/typestest"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_rust/ir"
)

// hostPlatform reproduces the same format os-arch variant as cipd does.
func hostPlatform() string {
	o := runtime.GOOS
	if o == "darwin" {
		o = "mac"
	}
	switch a := runtime.GOARCH; a {
	case "amd64":
		return o + "-x64"
	default:
		return o + "-" + a
	}
}

var rustFmtConfigFlag = flag.String("rustfmt-toml", "../../../.././rustfmt.toml", "Path to rustfmt.toml; only used in GN build")
var rustFmtFlag = flag.String("rustfmt", "../../../../prebuilt/third_party/rust_tools/"+hostPlatform()+"/bin", "Path to directory containing rustfmt; only used in GN build")
var testDataFlag = flag.String("test_data_dir", "../../../../garnet/go/src/fidl/compiler/backend/goldens", "Path to golden; only used in GN build")

type closeableBytesBuffer struct {
	bytes.Buffer
}

func (bb *closeableBytesBuffer) Close() error {
	return nil
}

func TestCodegen(t *testing.T) {
	for _, filename := range typestest.AllExamples(*testDataFlag) {
		t.Run(filename, func(t *testing.T) {
			tree := ir.Compile(typestest.GetExample(*testDataFlag, filename))
			want := typestest.GetGolden(*testDataFlag, fmt.Sprintf("%s.rs.golden", filename))
			buf := closeableBytesBuffer{}
			formatter := common.NewFormatter(filepath.Join(*rustFmtFlag, "rustfmt"), "--config-path", *rustFmtConfigFlag)
			actualFormattedImplDotRs, err := formatter.FormatPipe(&buf)
			if err != nil {
				t.Fatalf("unable to create format pipe: %s", err)
			}
			if err := NewGenerator().GenerateImpl(actualFormattedImplDotRs, tree); err != nil {
				t.Fatalf("unexpected error while generating impl.go: %s", err)
			}
			if err := actualFormattedImplDotRs.Close(); err != nil {
				t.Fatalf("unexpected error while closing formatter: %s", err)
			}
			typestest.AssertCodegenCmp(t, want, buf.Bytes())
		})
	}
}
