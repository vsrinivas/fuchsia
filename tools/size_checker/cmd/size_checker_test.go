// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"reflect"
	"strconv"
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
		data     string
		expected map[string]int64
	}{
		{
			"One Line", `[{"source_path":"","merkle": "foo","bytes":0,"size":0}]`, map[string]int64{"foo": 0},
		},
		{
			"Two Lines", `[{"source_path":"","merkle": "foo","bytes":0,"size":1},{"source_path":"","merkle": "bar","bytes":0,"size":2}]`, map[string]int64{"foo": 1, "bar": 2},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			filename := path.Join(t.TempDir(), "input.json")
			if err := ioutil.WriteFile(filename, []byte(test.data), 0600); err != nil {
				t.Fatal(err)
			}
			m, err := processBlobsJSON(filename)
			if err != nil {
				t.Fatal(err)
			}
			if !reflect.DeepEqual(m, test.expected) {
				t.Fatalf("processBlobsJSON() = %+v; expect %+v", m, test.expected)
			}
		})
	}
}
func Test_extractPackages(t *testing.T) {
	tests := []struct {
		name             string
		blobManifest     string
		expectedPackages []string
	}{
		{
			"No Meta Fars",
			"hash=foo",
			[]string{},
		},
		{
			"One Meta Far",
			"hash=one/meta.far",
			[]string{"one/meta.far"},
		},
		{
			"Multiple Meta Fars",
			"hash=foo\nhash=one/meta.far\nhash2=two/meta.far",
			[]string{"one/meta.far", "two/meta.far"},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			// Create the fake blob.manifest.
			tempdir := t.TempDir()
			blobManifestFile, err := os.Create(path.Join(tempdir, "blob.manifest"))
			if err != nil {
				t.Fatalf("Failed to create blob.manifest: %v", err)
			}
			if _, err := blobManifestFile.WriteString(test.blobManifest); err != nil {
				t.Fatalf("Failed to write blob.manifest: %v", err)
			}
			blobManifestFile.Close()

			// Extract the packages and check the results.
			p, err := extractPackages(tempdir, "blob.manifest")
			if err != nil {
				t.Fatalf("extractPackages returned an error: %+v", err)
			}
			if !reflect.DeepEqual(p, test.expectedPackages) {
				t.Fatalf("extractPackages(%s) = %+v; expect %+v", test.blobManifest, p, test.expectedPackages)
			}
		})
	}
}
func Test_addBlobsFromBlobsJSONToState(t *testing.T) {
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
			addBlobsFromBlobsJSONToState(&st, test.blobs, "")

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

func Test_addBlobsFromBlobsJSONToState_blobLookup(t *testing.T) {
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
			addBlobsFromBlobsJSONToState(&st, []BlobFromJSON{test.blob}, test.pkgPath)

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

func Test_calculateCompressedBlobSizeFromToolOutput(t *testing.T) {
	tests := []struct {
		name   string
		output string
		is_err bool
		size   int64
	}{
		{
			"Success",
			"Wrote 5 bytes",
			false,
			5,
		},
		{
			"Error",
			"Wrote bytes",
			true,
			0,
		},
		{
			"EmptyOutput",
			"",
			true,
			0,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			var bytes bytes.Buffer
			bytes.WriteString(test.output)
			size, err := calculateCompressedBlobSizeFromToolOutput(bytes)
			if !test.is_err && (err != nil) {
				t.Fatalf("Calculation returned an error when expected success: %s", err)
			} else if test.is_err && (err == nil) {
				t.Fatalf("Calculation returned success when expecting error")
			}
			if test.size != size {
				t.Fatalf("Calculation returns %d; expect %d", size, test.size)
			}
		})
	}
}

func Test_getBlobPathFromBlobsJson(t *testing.T) {
	blobs := []BlobFromJSON{
		{
			"11111",
			"path1",
			"source_root1",
		},
		{
			"22222",
			"path2",
			"source_root2",
		},
	}
	blob_paths := getBlobPathsFromBlobsJSON(blobs)
	if len(blob_paths) != 2 {
		t.Fatalf("getBlobPathsFromBlobsJSON returned invalid number of paths; expected 2")
	}
	if blob_paths[0] != "source_root1" {
		t.Fatalf("getBlobPathsFromBlobsJSON found %s in index 0; expected %s", blob_paths[0], "source_root1")
	}
	if blob_paths[1] != "source_root2" {
		t.Fatalf("getBlobPathsFromBlobsJSON found %s in index 1; expected %s", blob_paths[1], "source_root2")
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
			&Node{"foo", 10, 0, nil, make(map[string]*Node), nil},
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
	// Create the test size limits.
	const singleBlobSize = 4096
	fooSrcRelPath := "foo.src"
	input := SizeLimits{
		ICUDataLimit: json.Number("2048"),
		CoreLimit:    json.Number("2048"),
		ICUData:      []string{"icudtl.dat"},
		Components: []Component{
			{
				Component: "foo",
				Limit:     json.Number(strconv.Itoa(singleBlobSize)),
				Src:       []string{"foo-pkg"},
			},
		},
		NonBlobFSComponents: []NonBlobFSComponent{
			{
				Component:           "bar",
				Limit:               json.Number(strconv.Itoa(singleBlobSize)),
				PackageManifestPath: "obj/foo-pkg/package_manifest.json",
			},
		},
	}

	// Create the test directories.
	buildDir := t.TempDir()
	pkgDir := path.Join("obj", "foo-pkg")
	hostToolsDir := path.Join(buildDir, "host_x64")
	if err := os.MkdirAll(path.Join(buildDir, pkgDir), 0o700); err != nil {
		t.Fatalf("Failed to create package dir: %v", err)
	}
	if err := os.MkdirAll(hostToolsDir, 0o700); err != nil {
		t.Fatalf("Failed to create host tools dir: %v", err)
	}

	// Create the package manifest to test.
	packageManifestJSONF, err := os.Create(path.Join(buildDir, pkgDir, PackageManifest))
	if err != nil {
		t.Fatalf("Failed to create %s: %v", PackageManifest, err)
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
	packageManifest := PackageManifestJSON{
		Blobs: blobs,
	}
	packageManifestJSONBytes, err := json.Marshal(packageManifest)
	if err != nil {
		t.Fatalf("Failed to marshal JSON for %s: %v", PackageManifest, err)
	}
	if _, err := packageManifestJSONF.Write(packageManifestJSONBytes); err != nil {
		t.Fatalf("Failed to write %s: %v", PackageManifest, err)
	}
	packageManifestJSONF.Close()

	// Create the base package manifest.
	basePackageManifestRelPath := "base_package_manifest.json"
	basePackageManifestJSONF, err := os.Create(path.Join(buildDir, basePackageManifestRelPath))
	if err != nil {
		t.Fatalf("Failed to create base_package_manifest.json: %v", err)
	}
	baseBlobs := []BlobFromJSON{}
	basePackageManifest := PackageManifestJSON{
		Blobs: baseBlobs,
	}
	basePackageManifestJSONBytes, err := json.Marshal(basePackageManifest)
	if err != nil {
		t.Fatalf("Failed to marshal JSON for base_package_manifest.json: %v", err)
	}
	if _, err := basePackageManifestJSONF.Write(basePackageManifestJSONBytes); err != nil {
		t.Fatalf("Failed to write base_package_manifest.json: %v", err)
	}
	basePackageManifestJSONF.Close()

	// Create the blob.manifest.
	blobManifestRelPath := "blobs.manifest"
	blobManifestF, err := os.Create(path.Join(buildDir, blobManifestRelPath))
	if err != nil {
		t.Fatalf("Failed to create blob manifest file: %v", err)
	}
	blobManifest := fmt.Sprintf("deadbeef=%s\nabc123=%s\n", metaFarRelPath, fooSrcRelPath)
	if _, err := blobManifestF.Write([]byte(blobManifest)); err != nil {
		t.Fatalf("Failed to write blob manifest: %v", err)
	}
	blobManifestF.Close()

	// Create the blobs.json.
	blobSizeRelPath := "blobs.json"
	blobSizeF, err := os.Create(path.Join(buildDir, blobSizeRelPath))
	if err != nil {
		t.Fatalf("Failed to create blob size file: %v", err)
	}
	if _, err := blobSizeF.Write([]byte(fmt.Sprintf(`[{"source_path":"","merkle":"deadbeef","bytes":0,"size":%d},{"source_path":"","merkle":"abc123","bytes":0,"size":%d}]`, singleBlobSize, singleBlobSize))); err != nil {
		t.Fatalf("Failed to write blob sizes: %v", err)
	}
	blobSizeF.Close()

	// Create the blobfs-compression script.j
	blobfsCompressionFile := path.Join(buildDir, "host_x64", "blobfs-compression")
	blobfsCompression, err := os.Create(blobfsCompressionFile)
	if err != nil {
		t.Fatalf("Failed to create blobfs-compression script: %v", err)
	}
	if _, err := blobfsCompression.Write([]byte(fmt.Sprintf("#!/bin/bash\necho 'Wrote 5 bytes'\n"))); err != nil {
		t.Fatalf("Failed to write blobfs-compression: %v", err)
	}
	if err := os.Chmod(blobfsCompressionFile, 0o700); err != nil {
		t.Fatalf("Failed to make blobfs-compression executable: %v", err)
	}
	blobfsCompression.Close()

	// Run the function under test.
	sizes, err := parseSizeLimits(&input, buildDir, blobManifestRelPath, basePackageManifestRelPath, blobSizeRelPath)
	if err != nil {
		t.Fatal(err)
	}
	fooSize, ok := sizes["foo"]
	if !ok {
		t.Fatalf("Failed to find foo in sizes: %v", sizes)
	}
	if fooSize.Size != int64(2*singleBlobSize) {
		t.Fatalf("Unexpected size for component foo: %v", fooSize)
	}
	barSize, ok := sizes["bar"]
	if !ok {
		t.Fatalf("Failed to find bar in sizes: %v", sizes)
	}
	if barSize.Size != int64(10) {
		t.Fatalf("Unexpected size for component bar: %v", fooSize)
	}

	// Both the budget-only and full report should report going over-budget.
	budgetOnlyReportOverBudget, _ := generateReport(sizes, true, false, singleBlobSize*1024)
	fullReportOverBudget, _ := generateReport(sizes, false, false, singleBlobSize*1024)
	ignorePerComponentBudgetOverBudget, report := generateReport(sizes, false, true, singleBlobSize*1024)
	if !budgetOnlyReportOverBudget {
		t.Fatalf("The budget-only report is expected to report going overbudget.")
	}
	if !fullReportOverBudget {
		t.Fatalf("The full report is expected to report going overbudget.")
	}
	if ignorePerComponentBudgetOverBudget {
		t.Fatalf(
			"Ignoring per-component budget should not cause us to go overbudget. \n [%s]",
			report)
	}
}

func Test_writeOutputSizes(t *testing.T) {
	// Ensure that the output conforms to the schema
	// documented here:
	// https://chromium.googlesource.com/infra/gerrit-plugins/binary-size/+/HEAD/README.md
	sizes := map[string]*ComponentSize{
		"a": {
			Size:   1,
			Budget: 2,
			nodes:  []*Node{newNode("a node")},
		},
		"b": {
			Size:   2,
			Budget: 2,
			nodes:  []*Node{newNode("b node")},
		},
	}
	tmpDir := t.TempDir()
	outPath := filepath.Join(tmpDir, "sizes.json")
	if err := writeOutputSizes(sizes, outPath); err != nil {
		t.Fatalf("writeOutputSizes failed: %v", err)
	}
	wroteBytes, err := ioutil.ReadFile(outPath)
	if err != nil {
		t.Fatalf("ioutil.ReadFile() failed; %v", err)
	}
	var unmarshalled map[string]interface{}
	if err := json.Unmarshal(wroteBytes, &unmarshalled); err != nil {
		t.Errorf("json.Unmarshal() failed: %v", err)
	}

	if val, ok := unmarshalled["a"]; !ok || val.(float64) != 1 {
		t.Fatalf("json size output missing expected key/value entry for binary")
	}

	if val, ok := unmarshalled["a.budget"]; !ok || val.(float64) != 2 {
		t.Fatalf("json size output missing expected key/value entry for budget")
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
			"      hello                                                                      |   3.00 MiB           \n",
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
				t.Errorf("custom display mismatch:\nexpected: '%v'\n(bytes): %v\nactual:   '%v'\n(bytes): %v",
					test.expected,
					[]byte(test.expected),
					actual,
					[]byte(actual),
				)
			}
		})
	}
}
