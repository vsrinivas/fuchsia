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
	"sort"
	"strings"
)

type SizeLimits struct {
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

type ComponentSize struct {
	Size   int64 `json:"size"`
	Budget int64 `json:"budget"`
	nodes  []*Node
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
	MetaFar         = "meta.far"
	PackageList     = "gen/build/images/blob.manifest.list"
	BlobsJSON       = "blobs.json"
	ConfigData      = "config-data"
	DataPrefix      = "data/"
	SizeCheckerJSON = "size_checker.json"
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
func formatSize(sizeInBytes int64) string {
	sizeInMiB := float64(sizeInBytes) / (1024 * 1024)
	return fmt.Sprintf("%.2f MiB", sizeInMiB)
}

// Extract all the packages from a given blob.manifest.list and blobs.json.
// It also returns a map containing all blobs, with the merkle root as the key.
func extractPackages(buildDir, packageList, blobsJSON string) (blobMap map[string]*Blob, packages []string, err error) {
	blobMap = make(map[string]*Blob)

	var merkleRootToSizeMap map[string]int64
	if merkleRootToSizeMap, err = openAndProcessBlobsJSON(filepath.Join(buildDir, blobsJSON)); err != nil {
		return
	}

	f, err := os.Open(filepath.Join(buildDir, packageList))
	if err != nil {
		return
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		pkg, err := openAndProcessBlobsManifest(blobMap, merkleRootToSizeMap, buildDir, scanner.Text())
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
func openAndProcessBlobsManifest(
	blobMap map[string]*Blob,
	merkleRootToSizeMap map[string]int64,
	buildDir, blobsManifestFileName string) ([]string, error) {
	blobsManifestFile, err := os.Open(filepath.Join(buildDir, blobsManifestFileName))
	if err != nil {
		return nil, err
	}
	defer blobsManifestFile.Close()

	return processBlobsManifest(blobMap, merkleRootToSizeMap, blobsManifestFileName, blobsManifestFile), nil
}

// Similar to openAndProcessBlobsManifest, except it doesn't throw an I/O error.
func processBlobsManifest(
	merkleRootToBlobMap map[string]*Blob,
	merkleRootToSizeMap map[string]int64,
	blobsManifestFileName string, blobsManifestFile io.Reader) []string {
	packages := []string{}

	scanner := bufio.NewScanner(blobsManifestFile)
	for scanner.Scan() {
		temp := strings.Split(scanner.Text(), "=")
		merkleRoot := temp[0]
		fileName := temp[1]
		if blob, ok := merkleRootToBlobMap[merkleRoot]; !ok {
			blob = &Blob{
				dep:  []string{blobsManifestFileName},
				name: fileName,
				size: merkleRootToSizeMap[merkleRoot],
				hash: merkleRoot,
			}

			merkleRootToBlobMap[merkleRoot] = blob
			// This blob is a Fuchsia package.
			if strings.HasSuffix(temp[1], MetaFar) {
				packages = append(packages, temp[1])
			}
		} else {
			blob.dep = append(blob.dep, blobsManifestFileName)
		}
	}

	return packages
}

// Translates blobs.json into a map, with the key as the merkle root and the value as the size of
// that blob.
func openAndProcessBlobsJSON(blobsJSONFilePath string) (map[string]int64, error) {
	blobsJSONFile, err := os.Open(blobsJSONFilePath)
	if err != nil {
		return nil, err
	}
	defer blobsJSONFile.Close()

	return processBlobsJSON(blobsJSONFile)
}

func processBlobsJSON(blobsJSONFile io.Reader) (map[string]int64, error) {
	blobs := []BlobFromSizes{}
	if err := json.NewDecoder(blobsJSONFile).Decode(&blobs); err != nil {
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
		blobsJSON := filepath.Join(buildDir, dir, BlobsJSON)
		// We then parse the blobs.json
		blobs := []BlobFromJSON{}
		data, err := ioutil.ReadFile(blobsJSON)
		if err != nil {
			return fmt.Errorf(readError(blobsJSON, err))
		}
		if err := json.Unmarshal(data, &blobs); err != nil {
			return fmt.Errorf(unmarshalError(blobsJSON, err))
		}
		// Finally, we add the blob and the package to the tree.
		processBlobs(state, blobs, dir)
	}
	return nil
}

// Similar to processPackages except it doesn't throw an I/O error.
func processBlobs(
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

// Processes the given sizeLimits and throws an error if any component in the sizeLimits is above its
// allocated space limit.
func processSizeLimits(sizeLimits *SizeLimits, buildDir, packageList, blobsJSON string) map[string]*ComponentSize {
	outputSizes := map[string]*ComponentSize{}
	blobMap, packages, err := extractPackages(buildDir, packageList, blobsJSON)
	if err != nil {
		return outputSizes
	}

	// We create a set of extensions that should be considered as assets.
	assetMap := make(map[string]struct{})
	for _, ext := range sizeLimits.AssetExt {
		assetMap[ext] = struct{}{}
	}
	// We also create a map of paths that should be considered distributed shlibs.
	distributedShlibs := make(map[string]struct{})
	for _, path := range sizeLimits.DistributedShlibs {
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
		return outputSizes
	}

	var total int64
	root, err := st.root.getOnlyChild()
	if err != nil {
		return outputSizes
	}

	for _, component := range sizeLimits.Components {
		var size int64
		var nodes []*Node

		for _, src := range component.Src {
			if node := root.find(src); node != nil {
				nodes = append(nodes, node)
				size += node.size
			}
		}
		total += size
		sizeLimit, err := component.Limit.Int64()
		if err != nil {
			log.Fatalf("Failed to parse %s as an int64: %s\n", component.Limit, err)
		}
		outputSizes[component.Component] = &ComponentSize{
			Size:   size,
			Budget: sizeLimit,
			nodes:  nodes,
		}
	}

	AssetSizeLimit, err := sizeLimits.AssetLimit.Int64()
	if err != nil {
		log.Fatalf("Failed to parse %s as an int64: %s\n", sizeLimits.AssetLimit, err)
	}
	const assetsName = "Assets (Fonts / Strings / Images)"
	outputSizes[assetsName] = &ComponentSize{
		Size:   st.assetSize,
		Budget: AssetSizeLimit,
		nodes:  make([]*Node, 0),
	}

	CoreSizeLimit, err := sizeLimits.CoreLimit.Int64()
	if err != nil {
		log.Fatalf("Failed to parse %s as an int64: %s\n", sizeLimits.CoreLimit, err)
	}
	const coreName = "Core system+services"
	outputSizes[coreName] = &ComponentSize{
		Size:   root.size - total,
		Budget: CoreSizeLimit,
		nodes:  make([]*Node, 0),
	}

	if sizeLimits.DistributedShlibsLimit.String() != "" {
		DistributedShlibsSizeLimit, err := sizeLimits.DistributedShlibsLimit.Int64()
		if err != nil {
			log.Fatalf("Failed to parse %s as an int64: %s\n", sizeLimits.DistributedShlibsLimit, err)
		}
		const distributedShlibsName = "Distributed shared libraries"
		outputSizes[distributedShlibsName] = &ComponentSize{
			Size:   st.distributedShlibsSize,
			Budget: DistributedShlibsSizeLimit,
			nodes:  make([]*Node, 0),
		}
	}

	return outputSizes
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

func writeOutputSizes(sizes map[string]*ComponentSize, outPath string) error {
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
		fmt.Fprintln(os.Stderr, `Usage: size_checker [--budget-only] --build-dir BUILD_DIR [--sizes-json-out SIZES_JSON]

A executable that checks if any component from a build has exceeded its allocated space limit.

See //tools/size_checker for more details.`)
		flag.PrintDefaults()
	}
	var buildDir string
	flag.StringVar(&buildDir, "build-dir", "", `(required) the output directory of a Fuchsia build.`)
	var fileSizeOutPath string
	flag.StringVar(&fileSizeOutPath, "sizes-json-out", "", "If set, will write a json object to this path with schema { <name (str)>: <file size (int)> }.")
	var showBudgetOnly bool
	flag.BoolVar(&showBudgetOnly, "budget-only", false, "If set, only budgets and total sizes of components will be shown.")

	flag.Parse()

	if buildDir == "" {
		flag.Usage()
		os.Exit(2)
	}

	sizeCheckerJSON := filepath.Join(buildDir, SizeCheckerJSON)
	sizeCheckerJSONData, err := ioutil.ReadFile(sizeCheckerJSON)
	if err != nil {
		log.Fatal(readError(sizeCheckerJSON, err))
	}
	var sizeLimits SizeLimits
	if err := json.Unmarshal(sizeCheckerJSONData, &sizeLimits); err != nil {
		log.Fatal(unmarshalError(sizeCheckerJSON, err))
	}
	// If there are no components, then there are no work to do. We are done.
	if len(sizeLimits.Components) == 0 {
		os.Exit(0)
	}

	outputSizes := processSizeLimits(&sizeLimits, buildDir, PackageList, BlobsJSON)
	if len(fileSizeOutPath) > 0 {
		if err := writeOutputSizes(outputSizes, fileSizeOutPath); err != nil {
			log.Fatal(err)
		}
	}

	var overBudget = false
	var totalSize int64 = 0
	var totalBudget int64 = 0
	var totalRemaining int64 = 0
	var report strings.Builder
	componentNames := make([]string, 0, len(outputSizes))
	for componentName := range outputSizes {
		componentNames = append(componentNames, componentName)
	}
	sort.Strings(componentNames)
	report.WriteString("\n")
	report.WriteString(fmt.Sprintf("%-40s | %-10s | %-10s | %-10s\n", "Component", "Size", "Budget", "Remaining"))
	report.WriteString(strings.Repeat("-", 79) + "\n")
	for _, componentName := range componentNames {
		var componentSize = outputSizes[componentName]
		var remainingBudget = componentSize.Budget - componentSize.Size
		var colorCharacter = "\033[32m"
		if componentSize.Size > componentSize.Budget {
			overBudget = true
			colorCharacter = "\033[31m"
		}
		totalSize += componentSize.Size
		totalBudget += componentSize.Budget
		totalRemaining += remainingBudget
		report.WriteString(
			fmt.Sprintf("%-40s | %10s | %10s | %s%10s\033[0m\n", componentName, formatSize(componentSize.Size), formatSize(componentSize.Budget), colorCharacter, formatSize(remainingBudget)))
		if !showBudgetOnly {
			for _, n := range componentSize.nodes {
				report.WriteString(n.storageBreakdown(1))
			}
			report.WriteString("\n")
		}

	}
	report.WriteString(strings.Repeat("-", 79) + "\n")
	report.WriteString(fmt.Sprintf("%-40s | %10s | %10s | %10s\n", "Total", formatSize(totalSize), formatSize(totalBudget), formatSize(totalRemaining)))
	if overBudget {
		log.Fatal(report.String())
	} else {
		log.Println(report.String())
	}
}
