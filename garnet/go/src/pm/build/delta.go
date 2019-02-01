package build

import (
	"fmt"
	"sort"
	"strings"
)

// PackageFileRef is a tuple of a package name and a path within that package
type PackageFileRef struct {
	Name string `json:"name"`
	Path string `json:"path"`
}

func (r PackageFileRef) String() string {
	return fmt.Sprintf("%s/%s", r.Name, r.Path)
}

// DeltaBlobStats contains the size of a blob, its hash, and all paths to it
// from all packages.
type DeltaBlobStats struct {
	Merkle     MerkleRoot       `json:"merkle"`
	Size       uint64           `json:"size"`
	References []PackageFileRef `json:"references"`
}

// DeltaPackageBlobStats contains the size of a blob, its hash, and all paths
// to it within a particular package.
type DeltaPackageBlobStats struct {
	Merkle MerkleRoot `json:"merkle"`
	Size   uint64     `json:"size"`
	Paths  []string   `json:"paths"`
}

// PathsDisplay produces a string representation of DeltaPackageBlobStats.Paths
// suitable for display.
func (d *DeltaPackageBlobStats) PathsDisplay() string {
	if len(d.Paths) == 1 {
		return d.Paths[0]
	}
	return fmt.Sprintf("%s (%s)", d.Paths[0], strings.Join(d.Paths[1:], ", "))
}

// DeltaPackageStats contains statistics for a package update
type DeltaPackageStats struct {
	Name string `json:"name"`

	DownloadSize  uint64 `json:"download_size"`
	DiscardSize   uint64 `json:"discard_size"`
	UnchangedSize uint64 `json:"unchanged_size"`

	// AddedBlobs contains all blobs included in the target package that do
	// not exist in the source package, reverse sorted by blob size
	AddedBlobs []DeltaPackageBlobStats `json:"added_blobs"`
}

// SnapshotDelta contains update statistics from one Snapshot to another.
//
// All slices within a snapshot are canonically ordered as follows:
// 1. Reverse sorted by download size/file size
// 2. Sorted alphabetically by path/name/merkle
type SnapshotDelta struct {
	DownloadSize  uint64 `json:"download_size"`
	DiscardSize   uint64 `json:"discard_size"`
	UnchangedSize uint64 `json:"unchanged_size"`
	SourceSize    uint64 `json:"source_size"`
	TargetSize    uint64 `json:"target_size"`

	// AddedBlobs contains all blobs included in target that do not exist
	// in source, reverse sorted by blob size
	AddedBlobs []DeltaBlobStats `json:"added_blobs"`

	// Packages contains per-package update statistics, reverse sorted by
	// update size
	Packages []DeltaPackageStats `json:"packages"`
}

// ErrInconsistentSnapshotBlobs indicates that two package snapshots contain
// blobs with the same hash but different metadata. This situation should
// require a hash collision and be nearly impossible to encounter.
type ErrInconsistentSnapshotBlobs struct {
	Merkle MerkleRoot
	Source BlobInfo
	Target BlobInfo
}

// Error generates a display string for ErrInconsistentSnapshotBlobs
func (e ErrInconsistentSnapshotBlobs) Error() string {
	return fmt.Sprintf("blob %v inconsistent between source and target (%v != %v)", e.Merkle, e.Source, e.Target)
}

type packageFileRefByString []PackageFileRef

func (p packageFileRefByString) Len() int      { return len(p) }
func (p packageFileRefByString) Swap(i, j int) { p[i], p[j] = p[j], p[i] }
func (p packageFileRefByString) Less(i, j int) bool {
	if p[i].Name == p[j].Name {
		return strings.Compare(p[i].Path, p[j].Path) < 0
	}
	return strings.Compare(p[i].Name, p[j].Name) < 0
}

type deltaBlobStatsBySize []DeltaBlobStats

func (p deltaBlobStatsBySize) Len() int      { return len(p) }
func (p deltaBlobStatsBySize) Swap(i, j int) { p[i], p[j] = p[j], p[i] }
func (p deltaBlobStatsBySize) Less(i, j int) bool {
	if p[i].Size == p[j].Size {
		return p[i].Merkle.LessThan(p[j].Merkle)
	}
	return p[i].Size > p[j].Size
}

type deltaPackageBlobStatsBySize []DeltaPackageBlobStats

func (p deltaPackageBlobStatsBySize) Len() int      { return len(p) }
func (p deltaPackageBlobStatsBySize) Swap(i, j int) { p[i], p[j] = p[j], p[i] }
func (p deltaPackageBlobStatsBySize) Less(i, j int) bool {
	if p[i].Size == p[j].Size {
		return p[i].Merkle.LessThan(p[j].Merkle)
	}
	return p[i].Size > p[j].Size
}

type deltaPackageStatsByDownloadSize []DeltaPackageStats

func (p deltaPackageStatsByDownloadSize) Len() int      { return len(p) }
func (p deltaPackageStatsByDownloadSize) Swap(i, j int) { p[i], p[j] = p[j], p[i] }
func (p deltaPackageStatsByDownloadSize) Less(i, j int) bool {
	if p[i].DownloadSize == p[j].DownloadSize {
		return strings.Compare(p[i].Name, p[j].Name) < 0
	}
	return p[i].DownloadSize > p[j].DownloadSize
}

// sort canonically orders all slices in the delta according to download size and/or name.
func (delta *SnapshotDelta) sort() {
	sort.Sort(deltaBlobStatsBySize(delta.AddedBlobs))
	sort.Sort(deltaPackageStatsByDownloadSize(delta.Packages))
	for i := range delta.AddedBlobs {
		blob := &delta.AddedBlobs[i]
		sort.Sort(packageFileRefByString(blob.References))
	}
	for i := range delta.Packages {
		pkg := &delta.Packages[i]
		sort.Sort(deltaPackageBlobStatsBySize(pkg.AddedBlobs))
		for j := range pkg.AddedBlobs {
			blob := &pkg.AddedBlobs[j]
			sort.Strings(blob.Paths)
		}
	}
}

func invertFileMap(files map[string]MerkleRoot) map[MerkleRoot][]string {
	res := make(map[MerkleRoot][]string)
	for name, merkle := range files {
		val, _ := res[merkle]
		res[merkle] = append(val, name)
	}
	return res
}

// DeltaSnapshots compares two Snapshots, producing various statistics about an update from source to target
func DeltaSnapshots(source Snapshot, target Snapshot) (SnapshotDelta, error) {
	var delta SnapshotDelta

	// Ensure blob metadata is consistent between the snapshots
	for root, sourceInfo := range source.Blobs {
		if targetInfo, ok := target.Blobs[root]; ok {
			if sourceInfo != targetInfo {
				return delta, ErrInconsistentSnapshotBlobs{
					Merkle: root,
					Source: sourceInfo,
					Target: targetInfo,
				}
			}
		}
	}

	// Determine delta.*Size fields, find all new blobs
	for root, info := range target.Blobs {
		if _, ok := source.Blobs[root]; !ok {
			delta.DownloadSize += info.Size
			delta.AddedBlobs = append(delta.AddedBlobs, DeltaBlobStats{
				Merkle:     root,
				Size:       info.Size,
				References: nil,
			})
		}
		delta.TargetSize += info.Size
	}
	for root, info := range source.Blobs {
		if _, ok := target.Blobs[root]; !ok {
			delta.DiscardSize += info.Size
		}
		delta.SourceSize += info.Size
	}
	for root, info := range source.Blobs {
		if _, ok := target.Blobs[root]; ok {
			delta.UnchangedSize += info.Size
		}
	}

	// Populate delta.AddedBlobs[*].References
	{
		addedBlobMap := make(map[MerkleRoot]*DeltaBlobStats)
		for i, info := range delta.AddedBlobs {
			addedBlobMap[info.Merkle] = &delta.AddedBlobs[i]
		}
		for packageName, info := range target.Packages {
			for filePath, merkle := range info.Files {
				if blob, ok := addedBlobMap[merkle]; ok {
					ref := PackageFileRef{packageName, filePath}
					blob.References = append(blob.References, ref)
				}
			}
		}
	}

	// Compute per-package stats
	for packageName, targetInfo := range target.Packages {
		stats := DeltaPackageStats{
			Name: packageName,
		}
		for merkle, names := range invertFileMap(targetInfo.Files) {
			size := target.Blobs[merkle].Size
			if _, ok := source.Blobs[merkle]; ok {
				stats.UnchangedSize += size
			} else {
				blobInfo := DeltaPackageBlobStats{
					Merkle: merkle,
					Size:   size,
					Paths:  names,
				}
				stats.AddedBlobs = append(stats.AddedBlobs, blobInfo)
				stats.DownloadSize += size
			}
		}
		if sourceInfo, ok := source.Packages[packageName]; ok {
			for merkle := range invertFileMap(sourceInfo.Files) {
				if _, ok := target.Blobs[merkle]; !ok {
					stats.DiscardSize += source.Blobs[merkle].Size
				}
			}
		}
		delta.Packages = append(delta.Packages, stats)
	}

	delta.sort()

	return delta, nil
}
