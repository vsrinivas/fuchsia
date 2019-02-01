package build

import (
	"reflect"
	"sort"
	"testing"
)

// snapshotBuilder allows test cases to hand generate Snapshot structures
type snapshotBuilder struct {
	snapshot Snapshot

	currentBlobID MerkleRoot
}

type fileRef = PackageFileRef

func newSnapshotBuilder() *snapshotBuilder {
	return &snapshotBuilder{
		snapshot: Snapshot{
			Packages: map[string]Package{},
			Blobs:    map[MerkleRoot]BlobInfo{},
		},
	}
}

func (b *snapshotBuilder) ensurePackage(name string) {
	if _, ok := b.snapshot.Packages[name]; !ok {
		b.snapshot.Packages[name] = Package{
			Files: map[string]MerkleRoot{},
		}
	}
}

// IncrementMerkleRootEpoch moves to the start of the next block of merkle roots
func (b *snapshotBuilder) IncrementMerkleRootEpoch() *snapshotBuilder {
	b.currentBlobID[0]++
	b.currentBlobID[1] = 0
	return b
}

// IncrementMerkleRoot moves to the next sequential merkle root
func (b *snapshotBuilder) IncrementMerkleRoot() *snapshotBuilder {
	b.currentBlobID[1]++
	return b
}

// Package declares a new package with the given tags
func (b *snapshotBuilder) Package(pkg string, tags ...string) *snapshotBuilder {
	if _, ok := b.snapshot.Packages[pkg]; ok {
		panic("Package already defined")
	}
	b.snapshot.Packages[pkg] = Package{
		Files: map[string]MerkleRoot{},
		Tags:  tags,
	}
	return b
}

// File defines a new file with the given size, included at the given locations
func (b *snapshotBuilder) File(size uint64, refs ...fileRef) *snapshotBuilder {
	if len(refs) == 0 {
		panic("File called without any file refs")
	}
	b.IncrementMerkleRoot()
	b.snapshot.Blobs[b.currentBlobID] = BlobInfo{Size: size}

	for _, ref := range refs {
		b.ensurePackage(ref.Name)
		b.snapshot.Packages[ref.Name].Files[ref.Path] = b.currentBlobID
	}

	return b
}

// Build finalizes and returns the Snapshot
func (b *snapshotBuilder) Build() Snapshot {
	if err := b.snapshot.Verify(); err != nil {
		panic(err)
	}
	return b.snapshot
}

func makeTestSnapshot() Snapshot {
	return newSnapshotBuilder().
		Package("system/0", "monolith").
		Package("foo/0", "monolith").
		Package("bar/0", "monolith").
		Package("foobar/0", "monolith").
		Package("optional/0", "available").
		File(1024, fileRef{"system/0", "a"}).
		File(1024, fileRef{"system/0", "b"}).
		File(1024, fileRef{"system/0", "c"}).
		File(1024, fileRef{"system/0", "d"}).
		File(1024, fileRef{"system/0", "e"}).
		File(1024, fileRef{"system/0", "f"}).
		File(1024, fileRef{"system/0", "meta/"}).
		File(1024, fileRef{"foo/0", "bin/app"}).
		File(1024, fileRef{"foo/0", "fileA"}).
		File(1024, fileRef{"foo/0", "meta/"}).
		File(1024, fileRef{"bar/0", "bin/app"}).
		File(1024, fileRef{"bar/0", "fileB"}).
		File(1024, fileRef{"bar/0", "meta/"}).
		File(1024, fileRef{"foobar/0", "bin/app"}).
		File(1024, fileRef{"foobar/0", "meta/"}).
		File(1024, fileRef{"optional/0", "bin/app"}).
		File(1024, fileRef{"optional/0", "meta/"}).
		File(1024,
			fileRef{"foo/0", "lib/ld.so.1"},
			fileRef{"bar/0", "lib/ld.so.1"},
			fileRef{"foobar/0", "lib/ld.so.1"},
			fileRef{"optional/0", "lib/ld.so.1"},
		).
		File(1024,
			fileRef{"foo/0", "lib/libfdio.so"},
			fileRef{"bar/0", "lib/libfdio.so"},
			fileRef{"foobar/0", "lib/libfdio.so"},
			fileRef{"optional/0", "lib/libfdio.so"},
		).
		Build()
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
