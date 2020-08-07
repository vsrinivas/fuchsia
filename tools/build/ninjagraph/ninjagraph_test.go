// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjagraph

import (
	"flag"
	"os"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
)

var testDOTPath = flag.String("test_dot_path", "", "Path to the test DOT file")

func TestFromDOT(t *testing.T) {
	for _, tc := range []struct {
		dot  string
		want Graph
	}{
		{
			dot:  "digraph ninja {}",
			want: Graph{},
		},
		{
			dot: `digraph ninja {
rankdir="LR"
node [fontsize=10, shape=box, height=0.25]
edge [fontsize=10]
"0x1925e80" [label="foo.o"]
"0x1925f20" -> "0x1925e80" [label=" cc"]
"0x1925f20" [label="foo.c"]
"0x19261e0" [label="bar.o"]
"0x1926090" -> "0x19261e0" [label=" cc"]
"0x1926090" [label="bar.c"]
"0x1925f20" -> "0x1926090" [label=" cp"]
}`,

			want: Graph{
				NodeToPath: map[int64]string{
					0x1925f20: "foo.c",
					0x1925e80: "foo.o",
					0x1926090: "bar.c",
					0x19261e0: "bar.o",
				},
				Edges: map[int64][]int64{
					0x1925f20: {0x1925e80, 0x1926090},
					0x1926090: {0x19261e0},
				},
			},
		},
	} {
		got, err := FromDOT(strings.NewReader(tc.dot))
		if err != nil {
			t.Fatalf("FromDOT(%q) failed: %s", tc.dot, err)
		}
		if diff := cmp.Diff(tc.want, got, cmpopts.EquateEmpty(), cmpopts.SortSlices(func(x, y int64) bool { return x < y })); diff != "" {
			t.Errorf("FromDOT(%q) got diff (-want +got):\n%s", tc.dot, diff)
		}
	}
}

func TestFromDOTLargeFile(t *testing.T) {
	dotFile, err := os.Open(*testDOTPath)
	if err != nil {
		t.Fatalf("Failed to open file %q: %s", *testDOTPath, err)
	}
	defer dotFile.Close()

	got, err := FromDOT(dotFile)
	if err != nil {
		t.Fatalf("FromDOT('a large ninja graph') failed: %s", err)
	}
	// Since the graph is huge (the dot file has >1,300,000 lines), it is not
	// feasible to spell out the expected content of the parsed `Graph`, so we
	// only check number of nodes and edges.
	if gotNodes, wantNodes := len(got.NodeToPath), 189095; gotNodes != wantNodes {
		t.Errorf("FromDOT('a large ninja graph') got number of nodes: %d, want: %d", gotNodes, wantNodes)
	}

	var gotEdges int
	for _, edges := range got.Edges {
		gotEdges += len(edges)
	}
	if wantEdges := 1118948; gotEdges != wantEdges {
		t.Errorf("FromDOT('a large ninja graph') got number of edges: %d, want: %d", gotEdges, wantEdges)
	}
}

func BenchmarkFromDOT(b *testing.B) {
	for i := 0; i < b.N; i++ {
		dotFile, err := os.Open(*testDOTPath)
		if err != nil {
			b.Fatalf("Failed to open file %q: %s", *testDOTPath, err)
		}
		_, err = FromDOT(dotFile)
		dotFile.Close()
		if err != nil {
			b.Fatalf("FromDOT('a large ninja graph') failed: %s", err)
		}
	}
}
