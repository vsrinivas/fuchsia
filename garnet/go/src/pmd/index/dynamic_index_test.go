// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package index

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
	"syscall/zx"
	"testing"

	"fuchsia.googlesource.com/pm/pkg"
	"fuchsia.googlesource.com/pmd/amberer"
)

type packageFailure struct {
	merkles    []string
	status     zx.Status
	blobMerkle string
}

type fakeAmberClient struct {
	activated []string
	failed    []packageFailure
}

func (am *fakeAmberClient) PackagesActivated(merkles []string) {
	for _, m := range merkles {
		am.activated = append(am.activated, m)
	}
}

func (am *fakeAmberClient) PackagesFailed(merkles []string, status zx.Status, blobMerkle string) {
	am.failed = append(am.failed, packageFailure{merkles, status, blobMerkle})
}

func (am *fakeAmberClient) GetBlob(merkle string) {}

func newFakeAmberClient() *fakeAmberClient {
	return &fakeAmberClient{
		activated: make([]string, 0),
		failed:    make([]packageFailure, 0),
	}
}

var _ amberer.AmberClient = &fakeAmberClient{}

func TestList(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}

	pkgIndexPath := filepath.Join(d, "packages", "foo")
	err = os.MkdirAll(pkgIndexPath, os.ModePerm)
	if err != nil {
		t.Fatal(err)
	}
	err = ioutil.WriteFile(filepath.Join(pkgIndexPath, "0"), []byte{}, os.ModePerm)
	if err != nil {
		t.Fatal(err)
	}

	idx := NewDynamic(d, NewStatic(), newFakeAmberClient())
	pkgs, err := idx.List()
	if err != nil {
		t.Fatal(err)
	}

	if got, want := len(pkgs), 1; got != want {
		t.Errorf("got %d, want %d", got, want)
	}

	if got, want := pkgs[0].Name, "foo"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got, want := pkgs[0].Version, "0"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}

	pkgIndexPath = filepath.Join(d, "packages", "bar")
	err = os.MkdirAll(pkgIndexPath, os.ModePerm)
	if err != nil {
		t.Fatal(err)
	}
	err = ioutil.WriteFile(filepath.Join(pkgIndexPath, "1"), []byte{}, os.ModePerm)
	if err != nil {
		t.Fatal(err)
	}

	pkgs, err = idx.List()
	if err != nil {
		t.Fatal(err)
	}

	if got, want := len(pkgs), 2; got != want {
		t.Errorf("got %d, want %d", got, want)
	}

	if got, want := pkgs[0].Name, "bar"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got, want := pkgs[0].Version, "1"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got, want := pkgs[1].Name, "foo"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	if got, want := pkgs[1].Version, "0"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestAdd(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}

	idx := NewDynamic(d, NewStatic(), newFakeAmberClient())

	err = idx.Add(pkg.Package{Name: "foo", Version: "0"}, "abc")
	if err != nil {
		t.Fatal(err)
	}

	err = idx.Add(pkg.Package{Name: "foo", Version: "1"}, "def")
	if err != nil {
		t.Fatal(err)
	}

	err = idx.Add(pkg.Package{Name: "bar", Version: "10"}, "123")
	if err != nil {
		t.Fatal(err)
	}

	paths, err := filepath.Glob(filepath.Join(d, "packages/*/*"))
	if err != nil {
		t.Fatal(err)
	}

	sort.Strings(paths)

	for i := range paths {
		paths[i] = strings.TrimPrefix(paths[i], filepath.Join(d, "packages")+"/")
	}

	wantPaths := []string{"bar/10", "foo/0", "foo/1"}

	for i := range paths {
		if got, want := paths[i], wantPaths[i]; got != want {
			t.Errorf("got %q, want %q", got, want)
		}
	}
}

func TestFulfill(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}

	am := newFakeAmberClient()
	idx := NewDynamic(d, NewStatic(), am)

	// New package, no blobs pre-installed.
	{
		// Start installing package.
		neededBlobs := map[string]struct{}{
			"blob1": {},
			"blob2": {},
		}
		idx.Installing("root1")
		idx.UpdateInstalling("root1", pkg.Package{Name: "foo", Version: "1"})
		idx.AddNeeds("root1", neededBlobs)

		wantWaiting := map[string]struct{}{
			"blob1": {},
			"blob2": {},
		}
		if !reflect.DeepEqual(idx.waiting["root1"], wantWaiting) {
			t.Errorf("got %q, want %q", idx.waiting["root1"], wantWaiting)
		}
		wantNeeds := map[string]map[string]struct{}{
			"blob1": {"root1": {}},
			"blob2": {"root1": {}},
		}
		if !reflect.DeepEqual(idx.needs, wantNeeds) {
			t.Errorf("got %q, want %q", idx.needs, wantNeeds)
		}
		wantInstalling := pkg.Package{Name: "foo", Version: "1"}
		gotInstalling := idx.installing["root1"]
		if !reflect.DeepEqual(gotInstalling, wantInstalling) {
			t.Errorf("got %v, want %v", gotInstalling, wantInstalling)
		}

		// Fulfill one blob. Package is not done yet.
		idx.Fulfill("blob1")

		wantWaiting = make(map[string]struct{})
		wantWaiting["blob2"] = struct{}{}
		if !reflect.DeepEqual(idx.waiting["root1"], wantWaiting) {
			t.Errorf("got %q, want %q", idx.waiting["root1"], wantWaiting)
		}
		wantNeeds = map[string]map[string]struct{}{
			"blob2": {"root1": {}},
		}
		if !reflect.DeepEqual(idx.needs, wantNeeds) {
			t.Errorf("got %q, want %q", idx.needs, wantNeeds)
		}

		wantActivated := []string{}
		if !reflect.DeepEqual(am.activated, wantActivated) {
			t.Errorf("got %q, want %q", am.activated, wantActivated)
		}

		// Fulfill other blob. Package is done and appears in index.
		idx.Fulfill("blob2")
		if _, ok := idx.waiting["root1"]; ok {
			t.Errorf("root1 was not deleted from waiting")
		}
		wantNeeds = map[string]map[string]struct{}{}
		if !reflect.DeepEqual(idx.needs, wantNeeds) {
			t.Errorf("got %q, want %q", idx.needs, wantNeeds)
		}
		if _, ok := idx.installing["root1"]; ok {
			t.Errorf("root1 was not deleted from installing")
		}

		wantActivated = []string{"root1"}
		if !reflect.DeepEqual(am.activated, wantActivated) {
			t.Errorf("got %q, want %q", am.activated, wantActivated)
		}
		paths, err := filepath.Glob(filepath.Join(d, "packages/*/*"))
		if err != nil {
			t.Fatal(err)
		}
		for i := range paths {
			paths[i] = strings.TrimPrefix(paths[i], filepath.Join(d, "packages")+"/")
		}
		wantPaths := []string{"foo/1"}
		if !reflect.DeepEqual(paths, wantPaths) {
			t.Errorf("got %q, want %q", paths, wantPaths)
		}
	}

	// New package, one blob fails and later succeeds.
	{
		// Start installing package.
		neededBlobs := map[string]struct{}{
			"blob4": {},
		}
		idx.Installing("root2")
		idx.UpdateInstalling("root2", pkg.Package{Name: "bar", Version: "2"})
		idx.AddNeeds("root2", neededBlobs)

		wantWaiting := map[string]struct{}{
			"blob4": {},
		}
		if !reflect.DeepEqual(idx.waiting["root2"], wantWaiting) {
			t.Errorf("got %q, want %q", idx.waiting, wantWaiting)
		}
		wantNeeds := map[string]map[string]struct{}{
			"blob4": {"root2": {}},
		}
		if !reflect.DeepEqual(idx.needs, wantNeeds) {
			t.Errorf("got %q, want %q", idx.needs, wantNeeds)
		}
		wantInstalling := pkg.Package{Name: "bar", Version: "2"}
		gotInstalling := idx.installing["root2"]
		if !reflect.DeepEqual(gotInstalling, wantInstalling) {
			t.Errorf("got %v, want %v", gotInstalling, wantInstalling)
		}

		// Fail blob with error. Causes package failure to be signaled.
		idx.InstallingFailedForBlob("blob4", zx.ErrNoSpace)

		wantFailed := []packageFailure{
			{
				merkles:    []string{"root2"},
				status:     zx.ErrNoSpace,
				blobMerkle: "blob4",
			},
		}
		if !reflect.DeepEqual(am.failed, wantFailed) {
			t.Errorf("got %v, want %v", am.failed, wantFailed)
		}

		// Fulfill blob. Now the package should be marked finished.
		idx.Fulfill("blob4")

		if _, ok := idx.waiting["root2"]; ok {
			t.Errorf("root2 was not deleted from waiting")
		}
		wantNeeds = map[string]map[string]struct{}{}
		if !reflect.DeepEqual(idx.needs, wantNeeds) {
			t.Errorf("got %q, want %q", idx.needs, wantNeeds)
		}
		if _, ok := idx.installing["root2"]; ok {
			t.Errorf("root2 was not deleted from installing")
		}

		wantActivated := []string{"root1", "root2"}
		if !reflect.DeepEqual(am.activated, wantActivated) {
			t.Errorf("got %q, want %q", am.activated, wantActivated)
		}
		paths, err := filepath.Glob(filepath.Join(d, "packages/*/*"))
		if err != nil {
			t.Fatal(err)
		}
		for i := range paths {
			paths[i] = strings.TrimPrefix(paths[i], filepath.Join(d, "packages")+"/")
		}
		wantPaths := []string{"bar/2", "foo/1"}
		if !reflect.DeepEqual(paths, wantPaths) {
			t.Errorf("got %q, want %q", paths, wantPaths)
		}
	}
}
