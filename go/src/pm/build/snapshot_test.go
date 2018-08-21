package build

import (
	"reflect"
	"sort"
	"testing"
)

func makeMerkle(b byte) MerkleRoot {
	var m MerkleRoot
	for i := 0; i < len(m); i++ {
		m[i] = b
	}
	return m
}

func makeTestSnapshot() Snapshot {
	var merklePos byte
	nextMerkle := func() MerkleRoot {
		res := makeMerkle(merklePos)
		merklePos++
		return res
	}

	rootFdio := nextMerkle()
	rootLd := nextMerkle()

	snapshot := Snapshot{
		Packages: map[string]Package{
			"system/0": Package{
				Files: map[string]MerkleRoot{
					"a":     nextMerkle(),
					"b":     nextMerkle(),
					"c":     nextMerkle(),
					"d":     nextMerkle(),
					"e":     nextMerkle(),
					"f":     nextMerkle(),
					"meta/": nextMerkle(),
				},
				Tags: []string{"monolith"},
			},
			"foo/0": Package{
				Files: map[string]MerkleRoot{
					"bin/app":        nextMerkle(),
					"lib/ld.so.1":    rootLd,
					"lib/libfdio.so": rootFdio,
					"fileA":          nextMerkle(),
					"meta/":          nextMerkle(),
				},
				Tags: []string{"monolith"},
			},
			"bar/0": Package{
				Files: map[string]MerkleRoot{
					"bin/app":        nextMerkle(),
					"lib/ld.so.1":    rootLd,
					"lib/libfdio.so": rootFdio,
					"fileB":          nextMerkle(),
					"meta/":          nextMerkle(),
				},
				Tags: []string{"monolith"},
			},
			"foobar/0": Package{
				Files: map[string]MerkleRoot{
					"bin/app":        nextMerkle(),
					"lib/ld.so.1":    rootLd,
					"lib/libfdio.so": rootFdio,
					"meta/":          nextMerkle(),
				},
				Tags: []string{"monolith"},
			},
			"optional/0": Package{
				Files: map[string]MerkleRoot{
					"bin/app":        nextMerkle(),
					"lib/ld.so.1":    rootLd,
					"lib/libfdio.so": rootFdio,
					"meta/":          nextMerkle(),
				},
				Tags: []string{"available"},
			},
		},
		Blobs: make(map[MerkleRoot]BlobInfo),
	}

	for _, pkg := range snapshot.Packages {
		for _, root := range pkg.Files {
			snapshot.Blobs[root] = BlobInfo{Size: 1024}
		}
	}

	if err := snapshot.Verify(); err != nil {
		panic(err)
	}

	return snapshot
}

func verifyFilteredSnapshot(t *testing.T, original Snapshot, filtered Snapshot, expected []string) {
	// Ensure the correct blobs are present
	if err := filtered.Verify(); err != nil {
		t.Errorf("filter produced inconsistent snapshot: %v", err)
	}

	// Package entries should not change
	actual := []string{}
	for name, pkg := range filtered.Packages {
		actual = append(actual, name)
		if !reflect.DeepEqual(pkg, original.Packages[name]) {
			t.Errorf("filter modified a package entry")
		}
	}

	// Only the expected packages should be there
	sort.Strings(actual)
	sort.Strings(expected)
	if !reflect.DeepEqual(actual, expected) {
		t.Errorf("expected %v, got %v", expected, actual)
	}
}

func TestSnapshotFilter_includeAll(t *testing.T) {
	original := makeTestSnapshot()

	filtered := original.Filter([]string{"*"}, []string{})

	if !reflect.DeepEqual(original, filtered) {
		t.Errorf("identity filter did not produce an equivalent Snapshot")
	}
}

func TestSnapshotFilter_includeNone(t *testing.T) {
	original := makeTestSnapshot()

	filtered := original.Filter([]string{}, []string{})
	verifyFilteredSnapshot(t, original, filtered, []string{})
}

func TestSnapshotFilter_includeSingleTag(t *testing.T) {
	original := makeTestSnapshot()

	filtered := original.Filter([]string{"monolith"}, []string{})
	verifyFilteredSnapshot(t, original, filtered, []string{"system/0", "foo/0", "bar/0", "foobar/0"})

	filtered = original.Filter([]string{"available"}, []string{})
	verifyFilteredSnapshot(t, original, filtered, []string{"optional/0"})
}

func TestSnapshotFilter_matchSingleTag(t *testing.T) {
	original := makeTestSnapshot()

	filtered := original.Filter([]string{"monolith"}, []string{})
	verifyFilteredSnapshot(t, original, filtered, []string{"system/0", "foo/0", "bar/0", "foobar/0"})
}

func TestSnapshotFilter_excludeSingleTag(t *testing.T) {
	original := makeTestSnapshot()

	filtered := original.Filter([]string{"*"}, []string{"available"})
	verifyFilteredSnapshot(t, original, filtered, []string{"system/0", "foo/0", "bar/0", "foobar/0"})
}

func TestSnapshotFilter_excludeSinglePackage(t *testing.T) {
	original := makeTestSnapshot()

	filtered := original.Filter([]string{"*"}, []string{"bar/0"})
	verifyFilteredSnapshot(t, original, filtered, []string{"system/0", "foo/0", "optional/0", "foobar/0"})

	filtered = original.Filter([]string{"*"}, []string{"bar/*"})
	verifyFilteredSnapshot(t, original, filtered, []string{"system/0", "foo/0", "optional/0", "foobar/0"})
}

func TestSnapshotFilter_includeMultiplePackage(t *testing.T) {
	original := makeTestSnapshot()

	filtered := original.Filter([]string{"foo/0", "optional/0"}, []string{})
	verifyFilteredSnapshot(t, original, filtered, []string{"foo/0", "optional/0"})
}

func TestSnapshotFilter_excludeMultiplePackage(t *testing.T) {
	original := makeTestSnapshot()

	filtered := original.Filter([]string{"*"}, []string{"foo/0", "optional/0"})
	verifyFilteredSnapshot(t, original, filtered, []string{"system/0", "bar/0", "foobar/0"})
}

func TestSnapshotFilter_includeWildcardPackage(t *testing.T) {
	original := makeTestSnapshot()

	filtered := original.Filter([]string{"foo*"}, []string{})
	verifyFilteredSnapshot(t, original, filtered, []string{"foo/0", "foobar/0"})
}

func TestSnapshotFilter_excludeWildcardPackage(t *testing.T) {
	original := makeTestSnapshot()

	filtered := original.Filter([]string{"*"}, []string{"foo*"})
	verifyFilteredSnapshot(t, original, filtered, []string{"system/0", "bar/0", "optional/0"})
}
