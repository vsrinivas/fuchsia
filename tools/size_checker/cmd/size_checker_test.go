// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"reflect"
	"strconv"
	"strings"
	"testing"
)

// newNodeWithSize creates a node with p as name and a specified size.
func newNodeWithSize(p string, size int64) *Node {
	n := newNode(p)
	n.size = size
	return n
}

// setChildren sets the children of the given node.
func withSetChildren(n *Node, children map[string]*Node) *Node {
	n.children = children
	return n
}

func Test_processBlobsJSON(t *testing.T) {
	tests := []struct {
		name     string
		file     io.Reader
		expected map[string]int64
	}{
		{
			"One Line", strings.NewReader(`[{"source_path":"","merkle": "foo","bytes":0,"size":0}]`), map[string]int64{"foo": 0},
		},
		{
			"Two Lines", strings.NewReader(`[{"source_path":"","merkle": "foo","bytes":0,"size":1},{"source_path":"","merkle": "bar","bytes":0,"size":2}]`), map[string]int64{"foo": 1, "bar": 2},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			m, err := processBlobsJSON(test.file)
			if err != nil {
				t.Fatal(err)
			}
			if !reflect.DeepEqual(m, test.expected) {
				t.Fatalf("processBlobsJSON(%s) = %+v; expect %+v", test.file, m, test.expected)
			}
		})
	}
}
func Test_processBlobsManifest(t *testing.T) {
	tests := []struct {
		name            string
		blobMap         map[string]*Blob
		sizeMap         map[string]int64
		fileName        string
		file            io.Reader
		expectedPackage []string
		expectedBlobMap map[string]*Blob
	}{
		{
			"Adding New Blob To Empty Map",
			map[string]*Blob{},
			map[string]int64{"hash": 1},
			"fileFoo",
			strings.NewReader("hash=foo"),
			[]string{},
			map[string]*Blob{"hash": {dep: []string{"fileFoo"}, size: 1, name: "foo", hash: "hash"}},
		},
		{
			"Adding New Meta Far To Empty Map",
			map[string]*Blob{},
			map[string]int64{"hash": 1},
			"fileFoo",
			strings.NewReader("hash=meta.far"),
			[]string{"meta.far"},
			map[string]*Blob{"hash": {dep: []string{"fileFoo"}, size: 1, name: "meta.far", hash: "hash"}},
		},
		{
			"Adding A Blob To A Map With That Blob",
			map[string]*Blob{"hash": {dep: []string{"foo"}, size: 1, name: "foo", hash: "hash"}},
			map[string]int64{"hash": 1},
			"fileFoo",
			strings.NewReader("hash=foo"),
			[]string{},
			map[string]*Blob{"hash": {dep: []string{"foo", "fileFoo"}, size: 1, name: "foo", hash: "hash"}},
		},
		{
			"Adding A Meta Far To A Map With That Meta Far",
			map[string]*Blob{"hash": {dep: []string{"foo"}, size: 1, name: "meta.far", hash: "hash"}},
			map[string]int64{"hash": 1},
			"fileFoo",
			strings.NewReader("hash=meta.far"),
			[]string{},
			map[string]*Blob{"hash": {dep: []string{"foo", "fileFoo"}, size: 1, name: "meta.far", hash: "hash"}},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if p := parseBlobsManifest(test.blobMap, test.sizeMap, test.fileName, test.file); !reflect.DeepEqual(p, test.expectedPackage) {
				t.Fatalf("processBlobsManifest(%+v, %+v, %s, %+v) = %+v; expect %+v", test.blobMap, test.sizeMap, test.fileName, test.file, p, test.expectedPackage)
			}

			if !reflect.DeepEqual(test.blobMap["hash"], test.expectedBlobMap["hash"]) {
				t.Fatalf("blob map: %+v; expect %+v", test.blobMap["hash"], test.expectedBlobMap["hash"])
			}
		})
	}
}
func Test_processBlobs(t *testing.T) {
	tests := []struct {
		name                 string
		blobMap              map[string]*Blob
		icuDataMap           map[string]*Node
		distributedShlibsMap map[string]*Node
		blobs                []BlobFromJSON
		expectedBlobMap      map[string]*Blob
		expectedSize         int64
	}{
		{
			"Adding Asset Blob",
			map[string]*Blob{"hash": {size: 1}},
			map[string]*Node{"test.asset": {fullPath: "test.asset", size: 0, copies: 1, children: map[string]*Node{}}},
			map[string]*Node{"lib/ld.so.1": {fullPath: "lib/ld.so.1", size: 0, copies: 1, children: map[string]*Node{}}},
			[]BlobFromJSON{{Path: "test.asset", Merkle: "hash"}},
			map[string]*Blob{},
			1,
		},
		{
			"Adding Non-asset Blob",
			map[string]*Blob{"hash": {size: 1, dep: []string{"not used"}}},
			map[string]*Node{"test.asset": {fullPath: "test.asset", size: 0, copies: 1, children: map[string]*Node{}}},
			map[string]*Node{"lib/ld.so.1": {fullPath: "lib/ld.so.1", size: 0, copies: 1, children: map[string]*Node{}}},
			[]BlobFromJSON{{Path: "test.notasset", Merkle: "hash"}},
			map[string]*Blob{"hash": {size: 1, dep: []string{"not used"}}},
			0,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			st := processingState{
				test.blobMap,
				test.icuDataMap,
				test.distributedShlibsMap,
				newDummyNode(),
			}
			parseBlobsJSON(&st, test.blobs, "", "")

			if !reflect.DeepEqual(st.blobMap, test.expectedBlobMap) {
				t.Fatalf("blob map: %v; expect %v", test.blobMap, test.expectedBlobMap)
			}

			var totalIcuDataSize int64
			for _, node := range test.icuDataMap {
				totalIcuDataSize += node.size
			}
			if totalIcuDataSize != test.expectedSize {
				t.Fatalf("ICU Data size: %d; expect %d", totalIcuDataSize, test.expectedSize)
			}
		})
	}
}

func Test_processBlobsJSON_blobLookup(t *testing.T) {
	tests := []struct {
		name               string
		pkgPath            string
		blobMap            map[string]*Blob
		blob               BlobFromJSON
		expectedPathInTree string
	}{
		{
			"Adding non config-data blob",
			"path/to/pkg/non-config-data",
			map[string]*Blob{"hash": {size: 1, dep: []string{"not used"}}},
			BlobFromJSON{Path: "data/test/foo.txt", Merkle: "hash"},
			"path/to/pkg/non-config-data",
		},
		{
			"Adding config-data blob meta far",
			"path/to/pkg/config-data",
			map[string]*Blob{"hash": {size: 1, dep: []string{"not used"}}},
			BlobFromJSON{Path: "meta/", Merkle: "hash"},
			"path/to/pkg/config-data",
		},
		{
			"Adding config-data blob",
			"path/to/pkg/config-data",
			map[string]*Blob{"hash": {size: 1, dep: []string{"not used"}}},
			BlobFromJSON{Path: "data/test/foo.txt", Merkle: "hash"},
			"path/to/pkg/config-data/test/foo.txt",
		},
	}

	var dummyMap map[string]*Node

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			root := newDummyNode()
			st := processingState{
				test.blobMap,
				dummyMap,
				dummyMap,
				root,
			}
			parseBlobsJSON(&st, []BlobFromJSON{test.blob}, test.pkgPath, "")

			expectedNode := root.detachByPath(test.expectedPathInTree)
			if expectedNode == nil {
				t.Fatalf("tree.detachByPath(%s) returns nil; expect to find a node", test.expectedPathInTree)
			}

			expectedSize := test.blobMap[test.blob.Merkle].size
			if expectedNode.size != expectedSize {
				t.Fatalf("tree.detachByPath(%s).size returns %d; expect %d", test.expectedPathInTree, expectedNode.size, expectedSize)
			}
		})
	}
}
func Test_nodeAdd(t *testing.T) {
	root := newDummyNode()
	if root.size != 0 {
		t.Fatalf("the size of the root node is %d; expect 0", root.size)
	}
	testBlob := Blob{
		dep:  []string{"not used"},
		size: 10,
	}

	// Test adding a single node
	root.add("foo", &testBlob)
	if root.size != 10 {
		t.Fatalf("the size of the root node is %d; expect 10", root.size)
	}
	child := root.detachByPath("foo")
	if child == nil {
		t.Fatal("foo is not added as the child of the root")
	}
	if child.size != 10 {
		t.Fatalf("the size of the foo node (root's child) is %d; expect 10", child.size)
	}

	// Test adding a node that shares a common path
	root.add("foo/bar", &testBlob)
	if root.size != 10 {
		t.Fatalf("the size of the root node is %d; expect 10", root.size)
	}
	grandchild := root.detachByPath("foo/bar")
	if grandchild == nil {
		t.Fatal("bar is not added as the grandchild of the root")
	}
	if child.size != 10 {
		t.Fatalf("the size of the foo node (root's child) is %d; expect 10", child.size)
	}
	if grandchild.size != 10 {
		t.Fatalf("the size of the bar node (root's grandchild) is %d; expect 10", grandchild.size)
	}

	// Test adding a node with .meta suffix
	root.add("foo/update.meta", &testBlob)
	if root.size != 10 {
		t.Fatalf("the size of the root node is %d; expect 10", root.size)
	}
	update := root.detachByPath("foo/update")
	if update == nil {
		t.Fatal("update.meta is not added as the child of the root with the name 'update'")
	}
	if child.size != 10 {
		t.Fatalf("the size of the foo node (root's child) is %d; expect 10", child.size)
	}
	if update.size != 10 {
		t.Fatalf("the size of the update node (root's grandchild, bar's sibling) is %d; expect 10", update.size)
	}
}

func Test_nodeFind(t *testing.T) {
	tests := []struct {
		name     string
		path     string
		expected *Node
	}{
		{
			"Find Existing Node",
			"foo",
			newNodeWithSize("foo", 10),
		},
		{
			"Find Nonexistent Node",
			"NONEXISTENT",
			nil,
		},
	}
	root := newDummyNode()
	root.add("foo", &Blob{dep: []string{"not used"}, size: 10})

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if node := root.detachByPath(test.path); !reflect.DeepEqual(node, test.expected) {
				t.Fatalf("node.detachByPath(%s) = %+v; expect %+v", test.path, node, test.expected)
			}
		})
	}
}

func Test_processInput(t *testing.T) {
	const singleBlobSize = 4096
	fooSrcRelPath := "foo.src"
	input := SizeLimits{
		ICUDataLimit: json.Number("1"),
		CoreLimit:    json.Number("1"),
		ICUData:      []string{"icudtl.dat"},
		Components: []Component{
			{
				Component: "foo",
				Limit:     json.Number(strconv.Itoa(singleBlobSize)),
				Src:       []string{"foo-pkg"},
			},
		},
	}
	buildDir, err := ioutil.TempDir("", "out")
	if err != nil {
		t.Fatalf("Failed to create build dir: %v", err)
	}
	defer os.RemoveAll(buildDir)
	pkgDir := path.Join("obj", "foo-pkg")
	if err := os.MkdirAll(path.Join(buildDir, pkgDir), 0777); err != nil {
		t.Fatalf("Failed to create package dir: %v", err)
	}
	blobsJSONF, err := os.Create(path.Join(buildDir, pkgDir, BlobsJSON))
	if err != nil {
		t.Fatalf("Failed to create %s: %v", BlobsJSON, err)
	}
	metaFarRelPath := path.Join(pkgDir, MetaFar)
	blobs := []BlobFromJSON{
		{
			Merkle:     "deadbeef",
			Path:       "meta/",
			SourcePath: metaFarRelPath,
		},
		{
			Merkle:     "abc123",
			Path:       fooSrcRelPath,
			SourcePath: fooSrcRelPath,
		},
	}
	blobsJSONBytes, err := json.Marshal(blobs)
	if err != nil {
		t.Fatalf("Failed to marshal JSON for %s: %v", BlobsJSON, err)
	}
	if _, err := blobsJSONF.Write(blobsJSONBytes); err != nil {
		t.Fatalf("Failed to write %s: %v", BlobsJSON, err)
	}
	blobsJSONF.Close()
	blobManifestRelPath := path.Join(pkgDir, "blobs.manifest")
	blobManifestF, err := os.Create(path.Join(buildDir, blobManifestRelPath))
	if err != nil {
		t.Fatalf("Failed to create blob manifest file: %v", err)
	}
	blobManifest := fmt.Sprintf("deadbeef=%s\nabc123=%s\n", metaFarRelPath, fooSrcRelPath)
	if _, err := blobManifestF.Write([]byte(blobManifest)); err != nil {
		t.Fatalf("Failed to write blob manifest: %v", err)
	}
	blobManifestF.Close()
	blobListRelPath := "blob.manifest.list"
	blobListF, err := os.Create(path.Join(buildDir, blobListRelPath))
	if err != nil {
		t.Fatalf("Failed to create blob list file: %v", err)
	}
	if _, err := blobListF.Write([]byte(blobManifestRelPath)); err != nil {
		t.Fatalf("Failed to write blob list: %v", err)
	}
	blobListF.Close()
	blobSizeRelPath := "blobs.json"
	blobSizeF, err := os.Create(path.Join(buildDir, blobSizeRelPath))
	if err != nil {
		t.Fatalf("Failed to create blob size file: %v", err)
	}
	if _, err := blobSizeF.Write([]byte(fmt.Sprintf(`[{"source_path":"","merkle":"deadbeef","bytes":0,"size":%d},{"source_path":"","merkle":"abc123","bytes":0,"size":%d}]\n`, singleBlobSize, singleBlobSize))); err != nil {
		t.Fatalf("Failed to write blob sizes: %v", err)
	}
	blobSizeF.Close()
	sizes := parseSizeLimits(&input, buildDir, blobListRelPath, blobSizeRelPath)
	fooSize, ok := sizes["foo"]
	if !ok {
		t.Fatalf("Failed to find foo in sizes: %v", sizes)
	}
	if fooSize.Size != int64(2*singleBlobSize) {
		t.Fatalf("Unexpected size for component foo: %v", fooSize)
	}

	// Both the budget-only and full report should report going over-budget.
	budgetOnlyReportOverBudget, _ := generateReport(sizes, true, false, singleBlobSize*1024)
	fullReportOverBudget, _ := generateReport(sizes, false, false, singleBlobSize*1024)
	ignorePerComponentBudgetOverBudget, _ := generateReport(sizes, false, true, singleBlobSize*1024)
	if !budgetOnlyReportOverBudget {
		t.Fatalf("The budget-only report is expected to report going overbudget.")
	}
	if !fullReportOverBudget {
		t.Fatalf("The full report is expected to report going overbudget.")
	}
	if ignorePerComponentBudgetOverBudget {
		t.Fatalf("Ignoring per-component budget should not cause use to go overbudget.")
	}
}

func Test_writeOutputSizes(t *testing.T) {
	// Ensure that the output conforms to the schema
	// documented here:
	// https://chromium.googlesource.com/infra/gerrit-plugins/binary-size/+/HEAD/README.md
	sizes := map[string]*ComponentSize{
		"a": {
			Size:   1,
			Budget: 1,
			nodes:  []*Node{newNode("a node")},
		},
		"b": {
			Size:   2,
			Budget: 2,
			nodes:  []*Node{newNode("b node")},
		},
	}
	tmpDir, err := ioutil.TempDir("", "")
	if err != nil {
		t.Fatalf("ioutil.TempDir() failed: %v", err)
	}
	defer func() {
		if err := os.RemoveAll(tmpDir); err != nil {
			t.Error(err)
		}
	}()
	outPath := filepath.Join(tmpDir, "sizes.json")
	if err := writeOutputSizes(sizes, outPath); err != nil {
		t.Fatalf("writeOutputSizes failed: %v", err)
	}
	wroteBytes, err := ioutil.ReadFile(outPath)
	if err != nil {
		t.Fatalf("ioutil.ReadFile() failed; %v", err)
	}
	var unmarshalled map[string]int64
	if err := json.Unmarshal(wroteBytes, &unmarshalled); err != nil {
		t.Errorf("json.Unmarshal() failed: %v", err)
	}
}

func TestCustomDisplay(t *testing.T) {
	tests := []struct {
		node     *Node
		level    int
		expected string
	}{
		{
			newNodeWithSize("hello", 3*1024*1024),
			3,
			"      hello: 3.00 MiB \n",
		},
		{
			newNodeWithDisplay("hashAAAA", displayAsBlob),
			2,
			"    Blob ID hashAAAA (0 reuses):\n",
		},
		{
			newNodeWithDisplay("something_not_a_meta", displayAsMeta),
			2,
			"    something_not_a_meta\n",
		},
		{
			newNodeWithDisplay("/some/path/a_package.meta/something_else", displayAsMeta),
			2,
			"    a_package\n",
		},
		{
			withSetChildren(
				newNodeWithDisplay("hashAAAA", displayAsBlob),
				map[string]*Node{
					"1": newNodeWithDisplay("metaBBBB", displayAsMeta),
				}),
			2,
			"    Blob ID hashAAAA (1 reuses):\n      metaBBBB\n",
		},
	}
	for _, test := range tests {
		test := test
		t.Run(test.expected, func(t *testing.T) {
			t.Parallel()
			actual := test.node.storageBreakdown(test.level)
			if test.expected != actual {
				t.Errorf("custom display mismatch:\nexpected: '%v'\nactual:   '%v'", []byte(test.expected), []byte(actual))
			}
		})
	}
}
