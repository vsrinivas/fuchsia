// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

type FileSystemSizes []FileSystemSize

type FileSystemSize struct {
	Name  string      `json:"name"`
	Value json.Number `json:"value"`
	Limit json.Number `json:"limit"`
}

type SizeLimits struct {
	// Specifies a size limit in bytes for ICU data files.
	ICUDataLimit json.Number `json:"icu_data_limit"`
	// Specifies a size limit in bytes for uncategorized packages.
	CoreLimit json.Number `json:"core_limit"`
	// Specifies the files that contribute to the ICU data limit.
	ICUData []string `json:"icu_data"`
	// Specifies the files that contributed to the distributed shared library
	// size limits.
	DistributedShlibs []string `json:"distributed_shlibs"`
	// Specifies the distributed shared library size limit in bytes.
	DistributedShlibsLimit json.Number `json:"distributed_shlibs_limit"`
	// Specifies a series of size components/categories with a struct describing each.
	Components []Component `json:"components"`
	// Specifies a list of size components for blobs not added to blobfs.
	// This is useful when we want to check the size of packages that are
	// not added to BlobFS at assembly-time.
	NonBlobFSComponents []NonBlobFSComponent `json:"non_blobfs_components"`
}

// Display prints the contents of node and its children into a string.  All
// strings are printed at the supplied indentation level.
type DisplayFn func(node *Node, level int) string

type Node struct {
	fullPath string
	size     int64
	copies   int64
	parent   *Node
	children map[string]*Node
	// display is a function used to print the contents of Node in a human-friendly way.
	// If unset, a default display function is used.
	display DisplayFn
}

type Component struct {
	Component string      `json:"component"`
	Limit     json.Number `json:"limit"`
	Src       []string    `json:"src"`
}

type NonBlobFSComponent struct {
	Component           string      `json:"component"`
	Limit               json.Number `json:"limit"`
	PackageManifestPath string      `json:"blobs_json_path"`
}

type ComponentSize struct {
	Size             int64 `json:"size"`
	Budget           int64 `json:"budget"`
	IncludedInBlobFS bool
	nodes            []*Node
}

type OutputReport map[string]*ComponentSize

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

type PackageManifestJSON struct {
	Blobs []BlobFromJSON `json:"blobs"`
}

type PackageMetadata struct {
	Name    string `json:"string"`
	Version string `json:"version"`
}

type BlobFromJSON struct {
	Merkle     string `json:"merkle"`
	Path       string `json:"path"`
	SourcePath string `json:"source_path"`
}

type ComponentListReport struct {
	TotalConsumed  int64
	TotalBudget    int64
	TotalRemaining int64
	OverBudget     bool
	OutputStr      string
}

const (
	MetaFar             = "meta.far"
	BlobManifest        = "obj/build/images/fuchsia/fuchsia/gen/blob.manifest"
	RootBlobsJSON       = "obj/build/images/fuchsia/fuchsia/gen/blobs.json"
	BasePackageManifest = "obj/build/images/fuchsia/fuchsia/base_package_manifest.json"
	PackageManifest     = "package_manifest.json"
	BlobsJSON           = "blobs.json"
	ConfigData          = "config-data"
	DataPrefix          = "data/"
	SizeCheckerJSON     = "size_checker.json"
	FileSystemSizesJSON = "filesystem_sizes.json"
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

// newNodeWithDisplay creates a new Node, and supplies a custom function to be
// used to print its contents.
func newNodeWithDisplay(p string, display DisplayFn) *Node {
	n := newNode(p)
	n.display = display
	return n
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
	curr.size += size
	for _, name := range fullPath {
		name = strings.TrimSuffix(name, ".meta")
		nodePath = filepath.Join(nodePath, name)
		if _, ok := curr.children[name]; !ok {
			target := newNode(nodePath)
			target.parent = curr
			curr.children[name] = target
		}
		curr = curr.children[name]
		curr.size += size
	}
}

// Finds the node whose fullPath matches the given path. The path is guaranteed to be unique as it
// corresponds to the filesystem of the build directory. Detaches the node from the tree.
func (root *Node) detachByPath(p string) *Node {
	fullPath := strings.Split(p, "/")
	curr := root

	for _, name := range fullPath {
		next := curr.children[name]
		if next == nil {
			return nil
		}
		curr = next
	}
	curr.detach()
	return curr
}

// Detach this subtree from its parent, removing the size of this subtree from
// the aggregate size in the parent.
func (root *Node) detach() {
	size := root.size
	curr := root
	for curr.parent != nil {
		curr.parent.size -= size
		curr = curr.parent
	}
	if root.parent != nil {
		delete(root.parent.children, filepath.Base(root.fullPath))
	}
	root.parent = nil
}

// Returns the only child of a node. Useful for finding the root node.
func (node *Node) getOnlyChild() (*Node, error) {
	if len(node.children) == 1 {
		for _, child := range node.children {
			return child, nil
		}
	}

	return nil, fmt.Errorf("node does not contain a single child")
}

// displayAsDefault returns a human-readable representation of the supplied
// node and its children, using level as the indentation level for display
// pretty-printing.
func displayAsDefault(node *Node, level int) string {
	var copies string
	if node.copies > 1 {
		copies = fmt.Sprintf("| %3d %-6s", node.copies, "copies")
	} else if node.copies == 1 {
		copies = fmt.Sprintf("| %3d %-6s", node.copies, "copy")
	}

	var path = strings.TrimPrefix(node.fullPath, "obj/")
	path = strings.TrimPrefix(path, "lib/")
	if level > 1 {
		path = filepath.Base(path)
	}
	var maxLen = 80 - 2*level
	if maxLen < 0 {
		maxLen = 0
	}
	var pathLength = len(path)
	if pathLength > maxLen {
		var startPos = pathLength - maxLen + 3
		if startPos > pathLength {
			startPos = pathLength
		}
		path = "..." + path[startPos:]
	}
	path = fmt.Sprintf("%s%s", strings.Repeat("  ", level), path)
	ret := fmt.Sprintf("%-80s | %10s %10s\n", path, formatSize(node.size), copies)

	// Iterate over the childen in a sorted order.
	keys := make([]string, 0, len(node.children))
	for k := range node.children {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	for _, k := range keys {
		n := node.children[k]
		ret += n.storageBreakdown(level + 1)
	}
	return ret
}

// displayAsBlob returns a human-readable representation of the supplied Node and
// its children, formatted suitably for a blob ID, at the given indentation
// level.
func displayAsBlob(node *Node, level int) string {
	nc := len(node.children)
	ret := fmt.Sprintf("%vBlob ID %v (%v reuses):\n", strings.Repeat("  ", level), node.fullPath, nc)

	// Iterate over the childen in a sorted order.
	keys := make([]string, 0, len(node.children))
	for k := range node.children {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	for _, k := range keys {
		n := node.children[k]
		ret += n.storageBreakdown(level + 1)
	}
	return ret
}

// MetaRegex matches strings like: "/some/path/foo.meta/something_else" and
// grabs "foo" from them.
var metaRegex = regexp.MustCompile(`/([^/]+)\.meta/`)

// displayAsMeta returns a human-readable representation of the supplied Node and
// its children, formatted suitably as a metadata item, at the given indentation
// level.
func displayAsMeta(node *Node, level int) string {
	m := metaRegex.FindStringSubmatch(node.fullPath)
	var n string
	if len(m) == 0 || m[1] == "" {
		n = node.fullPath
	} else {
		n = m[1]
	}
	ret := fmt.Sprintf("%s%s\n", strings.Repeat("  ", level), n)
	// No children to iterate over.
	return ret
}

// storageBreakdown constructs a string including detailed storage
// consumption for the subtree of this node.
//
// `level` controls the indentation level to preserve hierarchy in the
// output.
func (node *Node) storageBreakdown(level int) string {
	if node.display == nil {
		// If the node has no custom display function.
		return displayAsDefault(node, level)
	}
	return node.display(node, level)
}

// Formats a given number into human friendly string representation of bytes, rounded to 2 decimal places.
func formatSize(sizeInBytes int64) string {
	sizeInMiB := float64(sizeInBytes) / (1024 * 1024)
	return fmt.Sprintf("%.2f MiB", sizeInMiB)
}

//////////////////////////////////
// 1. Collect the list of packages
//////////////////////////////////

// Iterate over the blob.manifest, searching for any metafars, and returning them.
// We expect each entry of blobs.manifest to have the following format:
// `$MERKLE_ROOT=$PATH_TO_BLOB`
func extractPackages(buildDir string, blobManifestFileName string) (packages []string, err error) {
	blobsManifestFile, err := os.Open(filepath.Join(buildDir, blobManifestFileName))
	if err != nil {
		return nil, readError(blobManifestFileName, err)
	}
	defer blobsManifestFile.Close()

	packages = []string{}
	blobsManifestScanner := bufio.NewScanner(blobsManifestFile)
	for blobsManifestScanner.Scan() {
		temp := strings.Split(blobsManifestScanner.Text(), "=")
		fileName := temp[1]

		// This blob is a Fuchsia package.
		if strings.HasSuffix(fileName, MetaFar) {
			packages = append(packages, fileName)
		}
	}

	return packages, nil
}

////////////////////////////////
// Calculating blob sizes
////////////////////////////////

type processingState struct {
	blobMap           map[string]*Blob
	icuDataMap        map[string]*Node
	distributedShlibs map[string]*Node
	root              *Node
}

// Translates blobs.json into a map, with the key as the merkle root and the value as the size of
// that blob.
func processBlobsJSON(blobsJSONFilePath string) (map[string]int64, error) {
	blobsJSONFile, err := os.Open(blobsJSONFilePath)
	if err != nil {
		return nil, readError(blobsJSONFilePath, err)
	}
	defer blobsJSONFile.Close()

	blobs := []BlobFromSizes{}
	if err := json.NewDecoder(blobsJSONFile).Decode(&blobs); err != nil {
		return nil, unmarshalError(blobsJSONFilePath, err)
	}

	m := map[string]int64{}
	for _, b := range blobs {
		m[b.Merkle] = int64(b.Size)
	}
	return m, nil
}

// Iterate over every package, and add their blobs to the blob map.
// The size of each blob gets divided by the number of dependencies,
// so that it does not get counted multiple times.
func calculateBlobSizes(
	state *processingState,
	buildDir string,
	blobManifest string,
	basePackageManifest string,
	rootBlobsJSON string,
	packages []string) error {

	// Find the compressed size of the blobs from the blobs.json produced by blobfs.
	var merkleRootToSizeMap map[string]int64
	merkleRootToSizeMap, err := processBlobsJSON(filepath.Join(buildDir, rootBlobsJSON))
	if err != nil {
		return fmt.Errorf("failed to parse the root blobs.json: %s", err)
	}

	var packageManifestFiles []string
	for _, metafar := range packages {
		packageManifestFile, err := getPackageManifestFileFromMetaFar(buildDir, blobManifest, metafar)
		if err != nil {
			return fmt.Errorf("failed to get the path of the package's blobs.json from %s: %s", metafar, err)
		}
		packageManifestFiles = append(packageManifestFiles, packageManifestFile)
	}

	// Manually add the base package manifest, because we do not yet have a way to
	// find it in the blob.manifest.
	// TODO: Determine a better way to find the base package manifest.
	packageManifestFiles = append(packageManifestFiles, basePackageManifest)

	// Add every blob to the blobMap, which will deduplicate the size across blobs.
	for _, packageManifestFile := range packageManifestFiles {
		blobs, err := readBlobsFromPackageManifest(filepath.Join(buildDir, packageManifestFile))
		if err != nil {
			return err
		}

		for _, blob := range blobs {
			blobState := state.blobMap[blob.Merkle]
			if blobState == nil {
				// Create the blob and insert.
				state.blobMap[blob.Merkle] = &Blob{
					dep:  []string{packageManifestFile},
					name: blob.SourcePath,
					size: merkleRootToSizeMap[blob.Merkle],
					hash: blob.Merkle,
				}
			} else {
				// Add another dep to the existing blobState.
				blobState.dep = append(blobState.dep, packageManifestFile)
			}
		}
	}

	// Add every blob to the tree of paths, which will add the size of the blob to
	// each package.
	for _, packageManifestFile := range packageManifestFiles {
		blobs, err := readBlobsFromPackageManifest(filepath.Join(buildDir, packageManifestFile))
		if err != nil {
			return err
		}

		// The package path used for matching the budgets is relative to the buildDir.
		pkgPath := filepath.Dir(packageManifestFile)

		// Add the blobs to the tree.
		addBlobsFromBlobsJSONToState(state, blobs, pkgPath)
	}

	return nil
}

// Get the path of the package's blobs.json which is sitting next to the metafar.
// Return this path relative to the buildDir
func getPackageManifestFileFromMetaFar(buildDir string, blobManifest string, metafar string) (string, error) {
	dir := filepath.Dir(metafar)

	// Get the absolute path of the package directory if not already an absolute path.
	// We need to do this because the directory is currently relative to the
	// blob.manifest and has a lot of ../../..
	// TODO: we should investigate why the path is sometimes an absolute path.
	if !filepath.IsAbs(dir) {
		blobManifestDir := filepath.Dir(blobManifest)
		absoluteDir, err := filepath.Abs(filepath.Join(buildDir, blobManifestDir, dir))
		if err != nil {
			return "", fmt.Errorf("failed to find the absolute path of the package directory %s: %s", dir, err)
		}
		dir = absoluteDir
	}

	// Get the absolute path of the buildDir
	if !filepath.IsAbs(buildDir) {
		absBuildDir, err := filepath.Abs(buildDir)
		if err != nil {
			return "", fmt.Errorf("failed to find the absolute path of the build directory %s: %s", buildDir, err)
		}
		buildDir = absBuildDir
	}

	// Rebase onto the buildDir.
	relDir, err := filepath.Rel(buildDir, dir)
	if err != nil {
		return "", fmt.Errorf("failed to make path %s relative to %s: %s", dir, buildDir, err)
	}

	return filepath.Join(relDir, PackageManifest), nil
}

// Iterate over all the blobs, and add them to the processing state.
func addBlobsFromBlobsJSONToState(
	state *processingState,
	blobs []BlobFromJSON,
	pkgPath string) {
	for _, blob := range blobs {
		// If the blob is an ICU data file, we don't add it to the tree.
		// We check the path instead of the source path because prebuilt packages have hashes as the
		// source path for their blobs
		baseBlobFilepath := filepath.Base(blob.Path)

		var (
			// Node must always be a pointer stored in state.icuDataMap, or nil.
			node *Node
			ok   bool
		)
		if node, ok = state.icuDataMap[baseBlobFilepath]; ok {
			// The size of each blob is the total space occupied by the blob in blobfs (each blob may be
			// referenced several times by different packages). Therefore, once we have already add the
			// size, we should remove it from the map
			if state.blobMap[blob.Merkle] != nil {
				if node == nil {
					state.icuDataMap[baseBlobFilepath] = newNode(baseBlobFilepath)
					// Ensure that node remains a pointer into the map.
					node = state.icuDataMap[baseBlobFilepath]
				}
				node.size += state.blobMap[blob.Merkle].size
				node.copies += 1
				delete(state.blobMap, blob.Merkle)
			}
			// Save the full path of the ICU data file, so ICU data file
			// proliferation can be debugged.
			var blobNode *Node
			blobNode = node.children[blob.Merkle]
			if blobNode == nil {
				blobNode = newNodeWithDisplay(blob.Merkle, displayAsBlob)
				node.children[blob.Merkle] = blobNode
			}
			icuCopyNode := newNodeWithDisplay(blob.SourcePath, displayAsMeta)
			blobNode.children[blob.SourcePath] = icuCopyNode
		} else if node, ok = state.distributedShlibs[blob.Path]; ok {
			if state.blobMap[blob.Merkle] != nil {
				if node == nil {
					state.distributedShlibs[blob.Path] = newNode(blob.Path)
					node = state.distributedShlibs[blob.Path]
				}
				node.size += state.blobMap[blob.Merkle].size
				node.copies += 1
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

////////////////////////////////////////////
// Calculate the size of non-blobfs packages
////////////////////////////////////////////

// Run an executable at |toolPath| and pass the |args|, then return the stdout.
func runToolAndCollectOutput(toolPath string, args []string) (bytes.Buffer, error) {
	cmd := exec.Command(toolPath, args...)
	var out bytes.Buffer
	cmd.Stdout = &out
	err := cmd.Run()
	return out, err
}

// Use the blobfs-compression tool at the provided path to calculate the size of
// the blob at |blobFilePath|.
func calculateCompressedBlobSize(blobfsCompressionToolPath string, blobFilePath string) (int64, error) {
	var source_file_arg = fmt.Sprintf("--source_file=%s", blobFilePath)
	out, err := runToolAndCollectOutput(blobfsCompressionToolPath, []string{source_file_arg})
	if err != nil {
		return 0, err
	}
	return calculateCompressedBlobSizeFromToolOutput(out)
}

// From the output of the blobfs-compression tool, |out|, parse the blob size,
// and return it.
func calculateCompressedBlobSizeFromToolOutput(out bytes.Buffer) (int64, error) {
	out_parts := bytes.Split(out.Bytes(), []byte(" "))
	if len(out_parts) < 2 {
		return 0, fmt.Errorf("output of blobfs-compression has an invalid format: %s", out.String())
	}
	size, err := strconv.Atoi(string(out_parts[1]))
	if err != nil {
		return 0, fmt.Errorf("failed to parse the blob size from blobfs-compression output: %s", out.String())
	}

	return int64(size), nil
}

// Read the package_manifest.json file at |packageManifestFile| and collect a
// slice of the blobs.
func readBlobsFromPackageManifest(packageManifestFile string) ([]BlobFromJSON, error) {
	var packageManifest PackageManifestJSON
	data, err := ioutil.ReadFile(packageManifestFile)
	if err != nil {
		return nil, readError(packageManifestFile, err)
	}
	if err := json.Unmarshal(data, &packageManifest); err != nil {
		return nil, unmarshalError(packageManifestFile, err)
	}
	return packageManifest.Blobs, nil
}

// Read the package_manifest.json file at |packageManifestFile| and return all
// the blobs referenced in it.
func getBlobPathsFromPackageManifest(packageManifestFile string) ([]string, error) {
	var blobs, err = readBlobsFromPackageManifest(packageManifestFile)
	if err != nil {
		return nil, err
	}
	return getBlobPathsFromBlobsJSON(blobs), nil
}

// Iterate over the |blobsJSON| struct, and return all the blobs.
func getBlobPathsFromBlobsJSON(blobsJSON []BlobFromJSON) []string {
	var blob_paths = []string{}
	for _, blob := range blobsJSON {
		blob_paths = append(blob_paths, blob.SourcePath)
	}
	return blob_paths
}

////////////////////////////////////////////
// Logic for checking size budgets
////////////////////////////////////////////

// Processes the given sizeLimits and populates an output report.
func parseSizeLimits(sizeLimits *SizeLimits, buildDir string, blobManifest string, basePackageManifest string, rootBlobsJSON string) (OutputReport, error) {
	outputSizes := make(OutputReport)
	// 1. Collect the list of packages.
	packages, err := extractPackages(buildDir, blobManifest)
	if err != nil {
		return nil, err
	}

	// We create a set of ICU data filenames.
	icuDataMap := make(map[string]*Node)
	for _, icu_data := range sizeLimits.ICUData {
		icuDataMap[icu_data] = newNode(icu_data)
	}

	// We also create a map of paths that should be considered distributed shlibs.
	distributedShlibs := make(map[string]*Node)
	for _, path := range sizeLimits.DistributedShlibs {
		distributedShlibs[path] = newNode(path)
	}

	// 2. Iterate over all the packages and add the blobs from each to the
	// processingState. This handles blob de-dup by dividing the size of each blob
	// by their number of dependencies.
	blobMap := make(map[string]*Blob)
	st := processingState{
		blobMap,
		icuDataMap,
		distributedShlibs,
		// The dummy node will have the root node as its only child.
		newDummyNode(),
	}
	if err := calculateBlobSizes(&st, buildDir, blobManifest, basePackageManifest, rootBlobsJSON, packages); err != nil {
		return nil, fmt.Errorf("failed to calculate the blob sizes: %s", err)
	}

	var distributedShlibsNodes []*Node
	var totalDistributedShlibsSize int64
	for _, node := range st.distributedShlibs {
		totalDistributedShlibsSize += node.size
		distributedShlibsNodes = append(distributedShlibsNodes, node)
	}

	var icuDataNodes []*Node
	var totalIcuDataSize int64
	for _, node := range st.icuDataMap {
		totalIcuDataSize += node.size
		icuDataNodes = append(icuDataNodes, node)
	}

	var total int64
	root, err := st.root.getOnlyChild()
	if err != nil {
		return nil, fmt.Errorf("failed to find the root node (typically obj): %s", err)
	}

	// 3. Iterate over every component, and detach the sizes at each mentioned
	// path, then add the size to the outputSizes.
	for _, component := range sizeLimits.Components {
		var size int64
		var nodes []*Node

		for _, src := range component.Src {
			if node := root.detachByPath(src); node != nil {
				nodes = append(nodes, node)
				size += node.size
			} else {
				// TODO: Make this fatal when all paths are accounted for.
				// Currently this is printed, because a couple paths are missing:
				log.Printf("WARN: failed to find blobs at the path: %s\n", src)
			}
		}
		total += size
		budget, err := component.Limit.Int64()
		if err != nil {
			return nil, parseError("component.Limit", err)
		}

		// There is only ever one copy of Update ZBIs.
		// TODO(fxbug.dev/58645): Delete once clients have removed the update
		// from their components list.
		if component.Component == "Update ZBIs" {
			budget /= 2
			size /= 2
		}
		outputSizes[component.Component] = &ComponentSize{
			Size:             size,
			Budget:           budget,
			IncludedInBlobFS: true,
			nodes:            nodes,
		}
	}

	var blobfsCompressionToolPath = filepath.Join(buildDir, "host_x64/blobfs-compression")
	for _, component := range sizeLimits.NonBlobFSComponents {
		// Collect the paths to each blob in the component.
		packageManifestPath := filepath.Join(buildDir, component.PackageManifestPath)
		var blobPaths, err = getBlobPathsFromPackageManifest(packageManifestPath)
		if err != nil {
			return nil, fmt.Errorf("failed to read the blob paths from the package manifest: %s", err)
		}

		// Sum all the individual blob sizes.
		var size = int64(0)
		var nodes []*Node
		for _, blobPath := range blobPaths {
			var fullBlobPath = filepath.Join(buildDir, blobPath)
			var blobSize, err = calculateCompressedBlobSize(blobfsCompressionToolPath, fullBlobPath)
			if err != nil {
				return nil, fmt.Errorf("failed to calculate the blob size: %s", err)
			}
			node := newNode(blobPath)
			node.size = blobSize
			nodes = append(nodes, node)
			size += blobSize
		}

		var budget int64
		budget, err = component.Limit.Int64()
		if err != nil {
			return nil, parseError("component.Limit", err)
		}

		// Add the sizes and budgets to the output.
		outputSizes[component.Component] = &ComponentSize{
			Size:             size,
			Budget:           budget,
			IncludedInBlobFS: false,
			nodes:            nodes,
		}
	}

	ICUDataLimit, err := sizeLimits.ICUDataLimit.Int64()
	if err != nil {
		return nil, parseError("ICUDataLimit", err)
	}
	const icuDataName = "ICU Data"
	outputSizes[icuDataName] = &ComponentSize{
		Size:             totalIcuDataSize,
		Budget:           ICUDataLimit,
		IncludedInBlobFS: true,
		nodes:            icuDataNodes,
	}

	CoreSizeLimit, err := sizeLimits.CoreLimit.Int64()
	if err != nil {
		return nil, parseError("CoreLimit", err)
	}
	const coreName = "Core system+services"
	coreNodes := make([]*Node, 0)
	// `root` contains the leftover nodes that have not been detached by the path
	// filters.
	coreNodes = append(coreNodes, root)
	outputSizes[coreName] = &ComponentSize{
		Size:             root.size,
		Budget:           CoreSizeLimit,
		IncludedInBlobFS: true,
		nodes:            coreNodes,
	}

	if sizeLimits.DistributedShlibsLimit.String() != "" {
		DistributedShlibsSizeLimit, err := sizeLimits.DistributedShlibsLimit.Int64()
		if err != nil {
			return nil, parseError("DistributedShlibsLimit", err)
		}

		const distributedShlibsName = "Distributed shared libraries"
		outputSizes[distributedShlibsName] = &ComponentSize{
			Size:             totalDistributedShlibsSize,
			Budget:           DistributedShlibsSizeLimit,
			IncludedInBlobFS: true,
			nodes:            distributedShlibsNodes,
		}
	}

	return outputSizes, nil
}

//////////////////////////////////
// Generate the report
//////////////////////////////////

func writeOutputSizes(sizes OutputReport, outPath string) error {
	f, err := os.Create(outPath)
	if err != nil {
		return fmt.Errorf("failed to create %s: %s", outPath, err)
	}

	encoder := json.NewEncoder(f)
	encoder.SetIndent("", "  ")
	simpleSizes := make(map[string]interface{})
	budgetSuffix := ".budget"
	// Owner/context links to provide shortcut to component specific size stats.
	ownerSuffix := ".owner"
	for name, cs := range sizes {
		simpleSizes[name] = cs.Size
		simpleSizes[name+budgetSuffix] = cs.Budget
		simpleSizes[name+ownerSuffix] = "http://go/fuchsia-size-stats/single_component/?f=component:in:" + url.QueryEscape(name)
	}
	if err := encoder.Encode(&simpleSizes); err != nil {
		_ = f.Close()
		return fmt.Errorf("failed to encode simpleSizes: %s", err)
	}
	return f.Close()
}

func generateComponentListOutput(outputSizes OutputReport, showBudgetOnly bool, ignorePerComponentBudget bool, includedInBlobFS bool) ComponentListReport {
	var overBudget = false
	var totalConsumed int64 = 0
	var totalBudget int64 = 0
	var totalRemaining int64 = 0
	var report strings.Builder
	componentNames := make([]string, 0, len(outputSizes))
	for componentName := range outputSizes {
		componentNames = append(componentNames, componentName)
	}
	sort.Strings(componentNames)
	report.WriteString("\n")
	if includedInBlobFS {
		report.WriteString(fmt.Sprintf("%-80s | %-10s | %-10s | %-10s\n", "Components included during assembly", "Size", "Budget", "Remaining"))
	} else {
		report.WriteString(fmt.Sprintf("%-80s | %-10s | %-10s | %-10s\n", "Components included post-assembly", "Size", "Budget", "Remaining"))

	}

	report.WriteString(strings.Repeat("-", 119) + "\n")
	for _, componentName := range componentNames {
		var componentSize = outputSizes[componentName]
		if componentSize.IncludedInBlobFS != includedInBlobFS {
			continue
		}

		var remainingBudget = componentSize.Budget - componentSize.Size
		var startColorCharacter string
		var endColorCharacter string

		// If any component is overbudget, then size_checker will fail.
		if componentSize.Size > componentSize.Budget && !ignorePerComponentBudget {
			overBudget = true
		}

		if showBudgetOnly {
			if componentSize.Size > componentSize.Budget {
				// Red
				startColorCharacter = "\033[31m"
			} else {
				// Green
				startColorCharacter = "\033[32m"
			}
			endColorCharacter = "\033[0m"
		}

		totalConsumed += componentSize.Size
		totalBudget += componentSize.Budget
		totalRemaining += remainingBudget

		report.WriteString(
			fmt.Sprintf("%-80s | %10s | %10s | %s%10s%s\n", componentName, formatSize(componentSize.Size), formatSize(componentSize.Budget), startColorCharacter, formatSize(remainingBudget), endColorCharacter))
		if !showBudgetOnly {
			for _, n := range componentSize.nodes {
				report.WriteString(n.storageBreakdown(1))
			}
			report.WriteString("\n")
		}

	}
	report.WriteString(strings.Repeat("-", 119) + "\n")

	return ComponentListReport{
		TotalConsumed:  totalConsumed,
		TotalBudget:    totalBudget,
		TotalRemaining: totalRemaining,
		OverBudget:     overBudget,
		OutputStr:      report.String(),
	}
}

func generateReport(outputSizes OutputReport, showBudgetOnly bool, ignorePerComponentBudget bool, blobFsCapacity int64) (bool, string) {

	var report strings.Builder
	var componentListReport = generateComponentListOutput(
		outputSizes, showBudgetOnly, ignorePerComponentBudget, true)
	var componentListReportPostAssembly = generateComponentListOutput(
		outputSizes, showBudgetOnly, ignorePerComponentBudget, false)

	report.WriteString(componentListReport.OutputStr)
	report.WriteString(componentListReportPostAssembly.OutputStr)

	var overBudget = componentListReport.OverBudget ||
		componentListReportPostAssembly.OverBudget
	var totalBudget = componentListReport.TotalBudget*2 +
		componentListReportPostAssembly.TotalBudget

	var totalConsumed = componentListReport.TotalConsumed*2 +
		componentListReportPostAssembly.TotalConsumed
	report.WriteString(
		fmt.Sprintf(
			"%-106s | %10s\n",
			"Components assembled into slot A:",
			formatSize(componentListReport.TotalConsumed)))
	report.WriteString(
		fmt.Sprintf(
			"%-106s | %10s\n",
			"Space reserved for slot B:",
			formatSize(componentListReport.TotalConsumed)))
	report.WriteString(
		fmt.Sprintf(
			"%-106s | %10s\n",
			"Components available post-assembly:",
			formatSize(componentListReportPostAssembly.TotalConsumed)))
	report.WriteString(
		strings.Repeat(" ", 108) + "+ " + strings.Repeat("-", 9) + "\n")
	report.WriteString(
		fmt.Sprintf(
			"%-106s | %10s\n",
			"Total Consumed:",
			formatSize(totalConsumed)))
	report.WriteString(
		fmt.Sprintf(
			"%-106s | %10s\n",
			"Total Budget:",
			formatSize(totalBudget)))
	report.WriteString(
		fmt.Sprintf(
			"%-106s | %10s\n",
			"Remaining Budget:",
			formatSize(totalBudget-totalConsumed)))
	report.WriteString(
		fmt.Sprintf(
			"%-106s | %10s\n",
			"Blobfs Capacity:",
			formatSize(blobFsCapacity)))
	report.WriteString(
		fmt.Sprintf(
			"%-106s | %10s\n",
			"Unallocated Budget:",
			formatSize(blobFsCapacity-totalBudget)))

	if totalConsumed > totalBudget {
		report.WriteString(
			fmt.Sprintf(
				"ERROR: Total data size [%s] exceeds total system data budget [%s]\n",
				formatSize(totalConsumed), formatSize(totalBudget)))
		overBudget = true
	}

	if totalBudget > blobFsCapacity && !ignorePerComponentBudget {
		report.WriteString(
			fmt.Sprintf(
				"WARNING: Total per-component data budget [%s] exceeds total system data budget [%s]\n",
				formatSize(totalBudget), formatSize(blobFsCapacity)))
		overBudget = true
	}
	return overBudget, report.String()
}

func parseBlobfsCapacity(buildDir, fileSystemSizesJSONFilename string) (int64, error) {
	fileSystemSizesJSON := filepath.Join(buildDir, fileSystemSizesJSONFilename)
	fileSystemSizesJSONData, err := ioutil.ReadFile(fileSystemSizesJSON)
	if err != nil {
		return 0, readError(fileSystemSizesJSON, err)
	}
	var fileSystemSizes = new(FileSystemSizes)
	if err := json.Unmarshal(fileSystemSizesJSONData, &fileSystemSizes); err != nil {
		return 0, unmarshalError(fileSystemSizesJSON, err)
	}
	for _, fileSystemSize := range *fileSystemSizes {
		if fileSystemSize.Name == "blob/capacity" {
			blobFsContentsSize, err := fileSystemSize.Limit.Int64()
			if err != nil {
				return 0, parseError("fileSystemSize.Limit", err)
			}
			return blobFsContentsSize, nil
		}
	}
	return 0, nil
}

//////////////////////////////////
// Common Errors
//////////////////////////////////

func readError(file string, err error) error {
	return verbError("read", file, err)
}

func unmarshalError(file string, err error) error {
	return verbError("unmarshal", file, err)
}

func parseError(field string, err error) error {
	return verbError("parse", field, err)
}

func verbError(verb, file string, err error) error {
	return fmt.Errorf("failed to %s %s: %s", verb, file, err)
}

//////////////////////////////////
// Entrypoint
//////////////////////////////////

func main() {
	flag.Usage = func() {
		fmt.Fprintln(os.Stderr, `Usage: size_checker [--budget-only] [--ignore-per-component-budget] --build-dir BUILD_DIR [--sizes-json-out SIZES_JSON]

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
	var ignorePerComponentBudget bool
	flag.BoolVar(&ignorePerComponentBudget, "ignore-per-component-budget", false,
		"If set, output will go to stderr only if the total size of components exceeds the total blobFs budget.")

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
	if len(sizeLimits.Components) == 0 && len(sizeLimits.NonBlobFSComponents) == 0 {
		os.Exit(0)
	}

	outputSizes, err := parseSizeLimits(&sizeLimits, buildDir, BlobManifest, BasePackageManifest, RootBlobsJSON)
	if err != nil {
		log.Fatal(err)
	}
	if len(fileSizeOutPath) > 0 {
		if err := writeOutputSizes(outputSizes, fileSizeOutPath); err != nil {
			log.Fatal(err)
		}
	}

	blobFsCapacity, err := parseBlobfsCapacity(buildDir, FileSystemSizesJSON)
	if err != nil {
		log.Fatal(err)
	}
	overBudget, report := generateReport(outputSizes, showBudgetOnly, ignorePerComponentBudget, blobFsCapacity)

	if overBudget {
		log.Fatal(report)
	} else {
		log.Println(report)
	}
}
