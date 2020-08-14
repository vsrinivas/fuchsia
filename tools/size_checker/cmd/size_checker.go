// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strings"
)

type Input struct {
	AssetLimit             json.Number `json:"asset_limit"`
	CoreLimit              json.Number `json:"core_limit"`
	AssetExt               []string    `json:"asset_ext"`
	DistributedShlibs      []string    `json:"distributed_shlibs"`
	DistributedShlibsLimit json.Number `json:"distributed_shlibs_limit"`
	Components             []Component `json:"components"`
}

type Node struct {
	fullPath string
	size     int64
	children map[string]*Node
}

type Component struct {
	Component string      `json:"component"`
	Limit     json.Number `json:"limit"`
	Src       []string    `json:"src"`
}

type Blob struct {
	dep  []string
	size int64
	name string
	hash string
}

type BlobFromSizes struct {
	SourcePath string `json:"source_path"`
	Merkle     string `json:"merkle"`
	Bytes      int    `json:"bytes"`
	Size       int    `json:"size"`
}

type BlobFromJSON struct {
	Merkle     string `json:"merkle"`
	Path       string `json:"path"`
	SourcePath string `json:"source_path"`
}

const (
	MetaFar    = "meta.far"
	BlobSizes  = "blobs.json"
	BlobList   = "gen/build/images/blob.manifest.list"
	BlobsJSON  = "blobs.json"
	ConfigData = "config-data"
	DataPrefix = "data/"
	InputJSON  = "size_checker.json"
)

func newDummyNode() *Node {
	return newNode("")
}

func newNode(p string) *Node {
	return &Node{
		fullPath: p,
		children: make(map[string]*Node),
	}
}

// Breaks down the given path divided by "/" and updates the size of each node on the path with the
// size of the given blob.
func (root *Node) add(p string, blob *Blob) {
	fullPath := strings.Split(p, "/")
	curr := root
	var nodePath string
	// A blob may be used by many packages, so the size of each blob is the total space allocated to
	// that blob in blobfs.
	// We divide the size by the total number of packages that depend on it to get a rough estimate of
	// the size of the individual blob.
	size := blob.size / int64(len(blob.dep))
	for _, name := range fullPath {
		name = strings.TrimSuffix(name, ".meta")
		nodePath = filepath.Join(nodePath, name)
		if _, ok := curr.children[name]; !ok {
			target := newNode(nodePath)
			curr.children[name] = target
		}
		curr = curr.children[name]
		curr.size += size
	}
}

// Finds the node whose fullPath matches the given path. The path is guaranteed to be unique as it
// corresponds to the filesystem of the build directory.
func (root *Node) find(p string) *Node {
	fullPath := strings.Split(p, "/")
	curr := root

	for _, name := range fullPath {
		next := curr.children[name]
		if next == nil {
			return nil
		}
		curr = next
	}

	return curr
}

// Returns the only child of a node. Useful for finding the root node.
func (node *Node) getOnlyChild() (*Node, error) {
	if len(node.children) == 1 {
		for _, child := range node.children {
			return child, nil
		}
	}

	return nil, fmt.Errorf("this node does not contain a single child.")
}

// storageBreakdown constructs a string including detailed storage
// consumption for the subtree of this node.
//
// `level` controls the indentation level to preserve hierarchy in the
// output.
func (node *Node) storageBreakdown(level int) string {
	ret := fmt.Sprintf("%s%s: %s (%d)\n", strings.Repeat("  ", level), node.fullPath, formatSize(node.size), node.size)
	for _, n := range node.children {
		ret += n.storageBreakdown(level + 1)
	}
	return ret
}

// Formats a given number into human friendly string representation of bytes, rounded to 2 decimal places.
func formatSize(s int64) string {
	size := float64(s)
	for _, unit := range []string{"bytes", "KiB", "MiB"} {
		if size < 1024 {
			return fmt.Sprintf("%.2f %s", size, unit)
		}
		size /= 1024
	}
	return fmt.Sprintf("%.2f GiB", size)
}

// Extract all the packages from a given blob.manifest.list and blobs.json.
// It also returns a map containing all blobs, with the merkle root as the key.
func extractPackages(buildDir, blobList, blobSize string) (blobMap map[string]*Blob, packages []string, err error) {
	blobMap = make(map[string]*Blob)

	var sizeMap map[string]int64
	if sizeMap, err = processBlobSizes(filepath.Join(buildDir, blobSize)); err != nil {
		return
	}

	f, err := os.Open(filepath.Join(buildDir, blobList))
	if err != nil {
		return
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		pkg, err := processBlobsManifest(blobMap, sizeMap, buildDir, scanner.Text())
		if err != nil {
			return blobMap, packages, err
		}

		packages = append(packages, pkg...)
	}

	return
}

// Opens a blobs.manifest file to populate the blob map and extract all meta.far blobs.
// We expect each entry of blobs.manifest to have the following format:
// `$MERKLE_ROOT=$PATH_TO_BLOB`
func processBlobsManifest(
	blobMap map[string]*Blob,
	sizeMap map[string]int64,
	buildDir, manifest string) ([]string, error) {
	f, err := os.Open(filepath.Join(buildDir, manifest))
	if err != nil {
		return nil, err
	}
	defer f.Close()

	return processManifest(blobMap, sizeMap, manifest, f), nil
}

// Similar to processBlobsManifest, except it doesn't throw an I/O error.
func processManifest(
	blobMap map[string]*Blob,
	sizeMap map[string]int64,
	manifest string, r io.Reader) []string {
	packages := []string{}

	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		temp := strings.Split(scanner.Text(), "=")
		if blob, ok := blobMap[temp[0]]; !ok {
			blob = &Blob{
				dep:  []string{manifest},
				name: temp[1],
				size: sizeMap[temp[0]],
				hash: temp[0],
			}

			blobMap[temp[0]] = blob
			// This blob is a Fuchsia package.
			if strings.HasSuffix(temp[1], MetaFar) {
				packages = append(packages, temp[1])
			}
		} else {
			blob.dep = append(blob.dep, manifest)
		}
	}

	return packages
}

// Translates blobs.json into a map, with the key as the merkle root and the value as the size of
// that blob.
func processBlobSizes(src string) (map[string]int64, error) {
	f, err := os.Open(src)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	return processSizes(f)
}

func processSizes(r io.Reader) (map[string]int64, error) {
	blobs := []BlobFromSizes{}
	if err := json.NewDecoder(r).Decode(&blobs); err != nil {
		return nil, err
	}

	m := map[string]int64{}
	for _, b := range blobs {
		m[b.Merkle] = int64(b.Size)
	}
	return m, nil
}

type processingState struct {
	blobMap               map[string]*Blob
	assetMap              map[string]struct{}
	assetSize             int64
	distributedShlibs     map[string]struct{}
	distributedShlibsSize int64
	root                  *Node
}

// Process the packages extracted from blob.manifest.list and process the blobs.json file to build a
// tree of packages.
func processPackages(
	buildDir string,
	packages []string,
	state *processingState) error {
	for _, metaFar := range packages {
		// From the meta.far file, we can get the path to the blobs.json for that package.
		dir := filepath.Dir(metaFar)
		blobJSON := filepath.Join(buildDir, dir, BlobsJSON)
		// We then parse the blobs.json
		blobs := []BlobFromJSON{}
		data, err := ioutil.ReadFile(blobJSON)
		if err != nil {
			return fmt.Errorf(readError(blobJSON, err))
		}
		if err := json.Unmarshal(data, &blobs); err != nil {
			return fmt.Errorf(unmarshalError(blobJSON, err))
		}
		// Finally, we add the blob and the package to the tree.
		processBlobsJSON(state, blobs, dir)
	}
	return nil
}

// Similar to processPackages except it doesn't throw an I/O error.
func processBlobsJSON(
	state *processingState,
	blobs []BlobFromJSON,
	pkgPath string) {
	for _, blob := range blobs {
		// If the blob is an asset, we don't add it to the tree.
		// We check the path instead of the source path because prebuilt packages have hashes as the
		// source path for their blobs
		if _, ok := state.assetMap[filepath.Ext(blob.Path)]; ok {
			// The size of each blob is the total space occupied by the blob in blobfs (each blob may be
			// referenced several times by different packages). Therefore, once we have already add the
			// size, we should remove it from the map
			if state.blobMap[blob.Merkle] != nil {
				state.assetSize += state.blobMap[blob.Merkle].size
				delete(state.blobMap, blob.Merkle)
			}
		} else if _, ok := state.distributedShlibs[blob.Path]; ok {
			if state.blobMap[blob.Merkle] != nil {
				state.distributedShlibsSize += state.blobMap[blob.Merkle].size
				delete(state.blobMap, blob.Merkle)
			}
		} else {
			var configPkgPath string
			if filepath.Base(pkgPath) == ConfigData && strings.HasPrefix(blob.Path, DataPrefix) {
				// If the package is config-data, we want to group the blobs by the name of the package
				// the config data is for.
				// By contract defined in config.gni, the path has the format 'data/$for_pkg/$outputs'
				configPkgPath = strings.TrimPrefix(blob.Path, DataPrefix)
			}
			state.root.add(filepath.Join(pkgPath, configPkgPath), state.blobMap[blob.Merkle])
		}
	}
}

// Processes the given input and throws an error if any component in the input is above its
// allocated space limit.
func processInput(input *Input, buildDir, blobList, blobSize string) (map[string]int64, string, bool) {
	outputSizes := map[string]int64{}
	blobMap, packages, err := extractPackages(buildDir, blobList, blobSize)
	if err != nil {
		return outputSizes, err.Error(), true
	}

	// We create a set of extensions that should be considered as assets.
	assetMap := make(map[string]struct{})
	for _, ext := range input.AssetExt {
		assetMap[ext] = struct{}{}
	}
	// We also create a map of paths that should be considered distributed shlibs.
	distributedShlibs := make(map[string]struct{})
	for _, path := range input.DistributedShlibs {
		distributedShlibs[path] = struct{}{}
	}
	st := processingState{
		blobMap,
		assetMap,
		0,
		distributedShlibs,
		0,
		// The dummy node will have the root node as its only child.
		newDummyNode(),
	}
	// We process the meta.far files that were found in the blobs.manifest here.
	if err := processPackages(buildDir, packages, &st); err != nil {
		return outputSizes, fmt.Sprintf("error processing packages: %s", err), true
	}

	var total int64
	var noSpace = false
	var report strings.Builder
	root, err := st.root.getOnlyChild()
	if err != nil {
		return outputSizes, err.Error(), true
	}

	for _, component := range input.Components {
		var size int64
		var nodes []*Node

		for _, src := range component.Src {
			if node := root.find(src); node != nil {
				nodes = append(nodes, node)
				size += node.size
			}
		}
		total += size
		outputSizes[component.Component] = size
		if s := checkLimit(component.Component, size, component.Limit); s != "" {
			noSpace = true
			report.WriteString(s + "\n")
		}
		for _, n := range nodes {
			report.WriteString(n.storageBreakdown(1))
		}
		report.WriteString("\n")
	}

	const assetsName = "Assets (Fonts / Strings / Images)"
	outputSizes[assetsName] = st.assetSize
	if s := checkLimit(assetsName, st.assetSize, input.AssetLimit); s != "" {
		noSpace = true
		report.WriteString(s + "\n")
	}

	const coreName = "Core system+services"
	outputSizes[coreName] = root.size - total
	if s := checkLimit(coreName, root.size-total, input.CoreLimit); s != "" {
		noSpace = true
		report.WriteString(s + "\n")
	}

	if input.DistributedShlibsLimit.String() != "" {
		const distributedShlibsName = "Distributed shared libraries"
		outputSizes[distributedShlibsName] = st.distributedShlibsSize
		if s := checkLimit(distributedShlibsName, st.distributedShlibsSize, input.DistributedShlibsLimit); s != "" {
			noSpace = true
			report.WriteString(s + "\n")
		}
	}

	return outputSizes, fmt.Sprintf(report.String()), noSpace
}

// Checks a given component to see if its size is greater than its allocated space limit as defined
// in the input JSON file.
func checkLimit(name string, size int64, limit json.Number) string {
	l, err := limit.Int64()
	if err != nil {
		log.Fatalf("failed to parse %s as an int64: %s\n", limit, err)
	}

	if size > l {
		return fmt.Sprintf("%s (%s) has exceeded its limit of %s.", name, formatSize(size), formatSize(l))
	}

	return ""
}

func readError(file string, err error) string {
	return verbError("read", file, err)
}

func unmarshalError(file string, err error) string {
	return verbError("unmarshal", file, err)
}

func verbError(verb, file string, err error) string {
	return fmt.Sprintf("Failed to %s %s: %s", verb, file, err)
}

func writeOutputSizes(sizes map[string]int64, outPath string) error {
	f, err := os.Create(outPath)
	if err != nil {
		return err
	}
	defer f.Close()

	encoder := json.NewEncoder(f)
	encoder.SetIndent("", "  ")
	if err := encoder.Encode(&sizes); err != nil {
		log.Fatal("failed to encode sizes: ", err)
	}
	return nil
}

func main() {
	flag.Usage = func() {
		fmt.Fprintln(os.Stderr, `Usage: size_checker --build-dir BUILD_DIR [--sizes-json-out SIZES_JSON]

A executable that checks if any component from a build has exceeded its allocated space limit.

See //tools/size_checker for more details.`)
		flag.PrintDefaults()
	}
	var buildDir string
	flag.StringVar(&buildDir, "build-dir", "", `(required) the output directory of a Fuchsia build.`)
	var fileSizeOutPath string
	flag.StringVar(&fileSizeOutPath, "sizes-json-out", "", "If set, will write a json object to this path with schema { <name (str)>: <file size (int)> }.")
	flag.Parse()

	if buildDir == "" {
		flag.Usage()
		os.Exit(2)
	}

	inputJSON := filepath.Join(buildDir, InputJSON)
	inputData, err := ioutil.ReadFile(inputJSON)
	if err != nil {
		log.Fatal(readError(inputJSON, err))
	}
	var input Input
	if err := json.Unmarshal(inputData, &input); err != nil {
		log.Fatal(unmarshalError(inputJSON, err))
	}
	// If there are no components, then there are no work to do. We are done.
	if len(input.Components) == 0 {
		os.Exit(0)
	}

	outputSizes, report, hasErr := processInput(&input, buildDir, BlobList, BlobSizes)
	if len(fileSizeOutPath) > 0 {
		if err := writeOutputSizes(outputSizes, fileSizeOutPath); err != nil {
			log.Fatal(err)
		}
	}
	if hasErr {
		log.Fatal(report)
	} else {
		log.Println(report)
	}
}
