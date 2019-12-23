package build

import (
	"reflect"
	"testing"
)

type mergedPackageFileRef struct {
	Name  string
	Paths []string
}

// mergePackageReferences transforms a slice of package/path tuples into a
// slice of package/[]path tuples, preserving member order. Member order must
// be preserved to uphold SnapshotDelta's invariant of member sort order.
func mergePackageReferences(references []PackageFileRef) []mergedPackageFileRef {
	res := []mergedPackageFileRef{}
	var current mergedPackageFileRef

	for _, ref := range references {
		if current.Name == "" {
			current.Name = ref.Name
		}

		if ref.Name == current.Name {
			current.Paths = append(current.Paths, ref.Path)
		} else {
			res = append(res, current)
			current.Name = ref.Name
			current.Paths = []string{ref.Path}
		}
	}

	return append(res, current)
}

// populatePackageAddedBlobs allows test cases to automatically generate any
// per-package AddedBlobs fields, since this data can be determined from the
// global AddedBlobs slice
func (d SnapshotDelta) populatePackageAddedBlobs() SnapshotDelta {
	packages := map[string]*DeltaPackageStats{}
	for i := range d.Packages {
		packages[d.Packages[i].Name] = &d.Packages[i]
	}

	for _, blob := range d.AddedBlobs {
		for _, ref := range mergePackageReferences(blob.References) {
			pkg := packages[ref.Name]
			pkg.AddedBlobs = append(pkg.AddedBlobs, DeltaPackageBlobStats{
				Merkle: blob.Merkle,
				Size:   blob.Size,
				Paths:  ref.Paths,
			})
		}
	}

	return d
}

// verifySnapshotDelta will delta source and target, and ensure the expected
// delta and computed delta match
func verifySnapshotDelta(t *testing.T, source Snapshot, target Snapshot, expected SnapshotDelta) {
	actual, err := DeltaSnapshots(source, target)
	if err != nil {
		t.Errorf("error producing snapshot delta: %v", err)
		return
	}

	if !reflect.DeepEqual(actual, expected) {
		t.Errorf("\nExpected:\n%v\n\nGot:\n%v", expected, actual)
	}
}

func TestDeltaSnapshots_inconsistentBlobs(t *testing.T) {
	source := newSnapshotBuilder().
		File(1, fileRef{"a", "file"}).
		Build()

	target := newSnapshotBuilder().
		File(2, fileRef{"different", "file?"}).
		Build()

	actual, err := DeltaSnapshots(source, target)
	if err == nil {
		t.Errorf("expected error, got %v", actual)
	}

	merkle := source.Packages["a"].Files["file"]
	expected := ErrInconsistentSnapshotBlobs{
		Merkle: merkle,
		Source: source.Blobs[merkle],
		Target: target.Blobs[merkle],
	}
	if err != expected {
		t.Errorf("expected %v, got %v", expected, err)
	}
}

func TestDeltaSnapshots_empty(t *testing.T) {
	empty := newSnapshotBuilder().Build()

	expected := SnapshotDelta{}

	verifySnapshotDelta(t, empty, empty, expected)
}

func TestDeltaSnapshots_same(t *testing.T) {
	s := makeTestSnapshot()

	expected := SnapshotDelta{
		UnchangedSize: s.Size(),
		SourceSize:    s.Size(),
		TargetSize:    s.Size(),
		Packages: []DeltaPackageStats{
			{
				Name:          "bar/0",
				UnchangedSize: 1024 * 5,
			},
			{
				Name:          "foo/0",
				UnchangedSize: 1024 * 5,
			},
			{
				Name:          "foobar/0",
				UnchangedSize: 1024 * 4,
			},
			{
				Name:          "optional/0",
				UnchangedSize: 1024 * 4,
			},
			{
				Name:          "system/0",
				UnchangedSize: 1024 * 7,
			},
		},
	}

	verifySnapshotDelta(t, s, s, expected)
}

func TestDeltaSnapshots_changeBlob(t *testing.T) {
	source := newSnapshotBuilder().
		File(1, fileRef{"a", "same"}).
		File(10, fileRef{"b", "same"}).
		File(100, fileRef{"b", "update"}).
		Build()

	target := newSnapshotBuilder().
		File(1, fileRef{"a", "same"}).
		File(10, fileRef{"b", "same"}).
		IncrementMerkleRootEpoch().
		File(200, fileRef{"b", "update"}).
		Build()

	expected := SnapshotDelta{
		DownloadSize:  200,
		DiscardSize:   100,
		UnchangedSize: 11,
		SourceSize:    111,
		TargetSize:    211,
		AddedBlobs: []DeltaBlobStats{
			{
				Merkle: target.Packages["b"].Files["update"],
				Size:   200,
				References: []PackageFileRef{
					{
						Name: "b",
						Path: "update",
					},
				},
			},
		},
		Packages: []DeltaPackageStats{
			{
				Name:          "b",
				DownloadSize:  200,
				DiscardSize:   100,
				UnchangedSize: 10,
			},
			{
				Name:          "a",
				UnchangedSize: 1,
			},
		},
	}.populatePackageAddedBlobs()

	verifySnapshotDelta(t, source, target, expected)
}

func TestDeltaSnapshots_addBlobs(t *testing.T) {
	source := newSnapshotBuilder().
		File(1, fileRef{"a", "same"}).
		File(10, fileRef{"b", "same"}).
		Build()

	target := newSnapshotBuilder().
		File(1, fileRef{"a", "same"}).
		File(10, fileRef{"b", "same"}).
		File(100, fileRef{"a", "new1"}).
		File(1000, fileRef{"b", "new2"}).
		Build()

	expected := SnapshotDelta{
		DownloadSize:  1100,
		DiscardSize:   0,
		UnchangedSize: 11,
		SourceSize:    11,
		TargetSize:    1111,
		AddedBlobs: []DeltaBlobStats{
			{
				Merkle: target.Packages["b"].Files["new2"],
				Size:   1000,
				References: []PackageFileRef{
					{
						Name: "b",
						Path: "new2",
					},
				},
			},
			{
				Merkle: target.Packages["a"].Files["new1"],
				Size:   100,
				References: []PackageFileRef{
					{
						Name: "a",
						Path: "new1",
					},
				},
			},
		},
		Packages: []DeltaPackageStats{
			{
				Name:          "b",
				DownloadSize:  1000,
				DiscardSize:   0,
				UnchangedSize: 10,
			},
			{
				Name:          "a",
				DownloadSize:  100,
				DiscardSize:   0,
				UnchangedSize: 1,
			},
		},
	}.populatePackageAddedBlobs()

	verifySnapshotDelta(t, source, target, expected)
}

func TestDeltaSnapshots_removeBlobs(t *testing.T) {
	source := newSnapshotBuilder().
		File(1, fileRef{"a", "same"}).
		File(10, fileRef{"b", "same"}).
		File(100, fileRef{"a", "new1"}).
		File(1000, fileRef{"b", "new2"}).
		Build()

	target := newSnapshotBuilder().
		File(1, fileRef{"a", "same"}).
		File(10, fileRef{"b", "same"}).
		Build()

	expected := SnapshotDelta{
		DownloadSize:  0,
		DiscardSize:   1100,
		UnchangedSize: 11,
		SourceSize:    1111,
		TargetSize:    11,
		Packages: []DeltaPackageStats{
			{
				Name:          "a",
				DiscardSize:   100,
				UnchangedSize: 1,
			},
			{
				Name:          "b",
				DiscardSize:   1000,
				UnchangedSize: 10,
			},
		},
	}.populatePackageAddedBlobs()

	verifySnapshotDelta(t, source, target, expected)
}

func TestDeltaSnapshots_updateAliasedBetweenPackages(t *testing.T) {
	source := newSnapshotBuilder().
		File(1,
			fileRef{"a", "blob1"},
			fileRef{"b", "blob2"},
		).
		Build()

	target := newSnapshotBuilder().
		IncrementMerkleRootEpoch().
		File(10,
			fileRef{"a", "blob1"},
			fileRef{"b", "blob2"},
		).
		Build()

	expected := SnapshotDelta{
		DownloadSize:  10,
		DiscardSize:   1,
		UnchangedSize: 0,
		SourceSize:    1,
		TargetSize:    10,
		AddedBlobs: []DeltaBlobStats{
			{
				Merkle: target.Packages["a"].Files["blob1"],
				Size:   10,
				References: []PackageFileRef{
					{
						Name: "a",
						Path: "blob1",
					},
					{
						Name: "b",
						Path: "blob2",
					},
				},
			},
		},
		Packages: []DeltaPackageStats{
			{
				Name:          "a",
				DownloadSize:  10,
				DiscardSize:   1,
				UnchangedSize: 0,
			},
			{
				Name:          "b",
				DownloadSize:  10,
				DiscardSize:   1,
				UnchangedSize: 0,
			},
		},
	}.populatePackageAddedBlobs()

	verifySnapshotDelta(t, source, target, expected)
}

func TestDeltaSnapshots_updateAliasedWithinPackage(t *testing.T) {
	source := newSnapshotBuilder().
		File(1000, fileRef{"a", "static"}).
		File(1,
			fileRef{"a", "file"},
			fileRef{"a", "samefile"},
		).
		Build()

	target := newSnapshotBuilder().
		File(1000, fileRef{"a", "static"}).
		IncrementMerkleRootEpoch().
		File(10,
			fileRef{"a", "file"},
			fileRef{"a", "samefile"},
		).
		Build()

	expected := SnapshotDelta{
		DownloadSize:  10,
		DiscardSize:   1,
		UnchangedSize: 1000,
		SourceSize:    1001,
		TargetSize:    1010,
		AddedBlobs: []DeltaBlobStats{
			{
				Merkle: target.Packages["a"].Files["file"],
				Size:   10,
				References: []PackageFileRef{
					{
						Name: "a",
						Path: "file",
					},
					{
						Name: "a",
						Path: "samefile",
					},
				},
			},
		},
		Packages: []DeltaPackageStats{
			{
				Name:          "a",
				DownloadSize:  10,
				DiscardSize:   1,
				UnchangedSize: 1000,
			},
		},
	}.populatePackageAddedBlobs()

	verifySnapshotDelta(t, source, target, expected)

}

func TestDeltaSnapshots_addRemoveAliased(t *testing.T) {
	source := newSnapshotBuilder().
		File(1, fileRef{"a", "remove"}).
		File(3, fileRef{"b", "remove"}).
		Build()

	target := newSnapshotBuilder().
		IncrementMerkleRootEpoch().
		File(10,
			fileRef{"a", "add"},
			fileRef{"b", "add"},
		).
		Build()

	expected := SnapshotDelta{
		DownloadSize:  10,
		DiscardSize:   4,
		UnchangedSize: 0,
		SourceSize:    4,
		TargetSize:    10,
		AddedBlobs: []DeltaBlobStats{
			{
				Merkle: target.Packages["a"].Files["add"],
				Size:   10,
				References: []PackageFileRef{
					{
						Name: "a",
						Path: "add",
					},
					{
						Name: "b",
						Path: "add",
					},
				},
			},
		},
		Packages: []DeltaPackageStats{
			{
				Name:          "a",
				DownloadSize:  10,
				DiscardSize:   1,
				UnchangedSize: 0,
			},
			{
				Name:          "b",
				DownloadSize:  10,
				DiscardSize:   3,
				UnchangedSize: 0,
			},
		},
	}.populatePackageAddedBlobs()

	verifySnapshotDelta(t, source, target, expected)
}

func TestDeltaSnapshots_addPackage(t *testing.T) {
	source := newSnapshotBuilder().
		File(1, fileRef{"a", "shared"}).
		File(10, fileRef{"a", "unique_a"}).
		Build()

	target := newSnapshotBuilder().
		File(1,
			fileRef{"a", "shared"},
			fileRef{"b", "shared"},
		).
		File(10, fileRef{"a", "unique_a"}).
		File(100, fileRef{"b", "unique_b"}).
		Build()

	expected := SnapshotDelta{
		DownloadSize:  100,
		DiscardSize:   0,
		UnchangedSize: 11,
		SourceSize:    11,
		TargetSize:    111,
		AddedBlobs: []DeltaBlobStats{
			{
				Merkle: target.Packages["b"].Files["unique_b"],
				Size:   100,
				References: []PackageFileRef{
					{
						Name: "b",
						Path: "unique_b",
					},
				},
			},
		},
		Packages: []DeltaPackageStats{
			{
				Name:          "b",
				DownloadSize:  100,
				DiscardSize:   0,
				UnchangedSize: 1,
			},
			{
				Name:          "a",
				DownloadSize:  0,
				DiscardSize:   0,
				UnchangedSize: 11,
			},
		},
	}.populatePackageAddedBlobs()

	verifySnapshotDelta(t, source, target, expected)
}

func TestDeltaSnapshots_removePackage(t *testing.T) {
	source := newSnapshotBuilder().
		File(1,
			fileRef{"a", "shared"},
			fileRef{"b", "shared"},
		).
		File(10, fileRef{"a", "unique_a"}).
		File(100, fileRef{"b", "unique_b"}).
		Build()

	target := newSnapshotBuilder().
		File(1, fileRef{"a", "shared"}).
		File(10, fileRef{"a", "unique_a"}).
		Build()

	expected := SnapshotDelta{
		DownloadSize:  0,
		DiscardSize:   100,
		UnchangedSize: 11,
		SourceSize:    111,
		TargetSize:    11,
		Packages: []DeltaPackageStats{
			{
				Name:          "a",
				DownloadSize:  0,
				DiscardSize:   0,
				UnchangedSize: 11,
			},
			// SnapshotDelta.Packages does not contain
			// DeltaPackageStats entries for packages present only
			// in source.

			// DeltaPackageStats{
			// 	Name:          "b",
			// 	DownloadSize:  0,
			// 	DiscardSize:   100,
			// 	UnchangedSize: 1,
			// },
		},
	}.populatePackageAddedBlobs()

	verifySnapshotDelta(t, source, target, expected)
}
