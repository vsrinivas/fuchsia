// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"context"
	"fmt"
	"strings"

	"cloud.google.com/go/storage"
	"google.golang.org/api/iterator"
)

// Directory is a handle to a Cloud Storage "directory". It provides a minimal
// filesystem-like interface for a Cloud Storage object hierarchy where "/" is used as the
// path separator. Any methods added to this struct are forward to other directory types.
type Directory struct {
	bucket *storage.BucketHandle
	root   string
}

// Object returns a handle to the given object within this directory. path is the path to
// the object relative to this directory.
func (d *Directory) Object(path string) *storage.ObjectHandle {
	path = fmt.Sprintf("%s/%s", d.root, path)
	return d.bucket.Object(path)
}

// List lists all of the objects in this directory.
func (d *Directory) List(ctx context.Context) ([]string, error) {
	prefix := strings.Join([]string{d.root}, "/")
	iter := d.bucket.Objects(ctx, &storage.Query{
		Prefix: prefix,
	})

	var items []string
	for {
		attrs, err := iter.Next()
		if err == iterator.Done {
			break
		}
		if err != nil {
			return nil, err
		}
		items = append(items, strings.TrimPrefix(attrs.Name, prefix+"/"))
	}

	return items, nil
}

// CD returns a handle to some child Directory of this Directory. It is up to the caller
// to ensure that child exists and is actually a directory.
func (d *Directory) cd(child string) *Directory {
	return &Directory{
		bucket: d.bucket,
		root:   fmt.Sprintf("%s/%s", d.root, child),
	}
}

// BuildDirectory represents a Fuchsia CI build's artifact directory. Refer to the
// layout in doc.go for the layout of this directory. When amending the layout, prefer
// adding methods to create new subdirectories on this type instead of calling Object()
// with paths containing slashes. This encourages all clients of this package create
// objects within this BuildDirectory using the same layout.
type BuildDirectory struct {
	*Directory
}

// Test returns the TestDirectory containing artifacts for a particular test. The name is
// normalized according to normalizePathSegment.
func (d BuildDirectory) Test(name string) *TestDirectory {
	subdir := fmt.Sprintf("tests/%s", normalizePathSegment(name))
	return &TestDirectory{d.Directory.cd(subdir)}
}

// TestDirectory contains artifacts for a particular test.
type TestDirectory struct {
	*Directory
}

// Env returns a Directory for objects relevant to an execution of this TestDirectory's
// test in a particular environment. The name is normalized according to
// normalizePathSegment.
func (d TestDirectory) Env(name string) *Directory {
	return d.Directory.cd(normalizePathSegment(name))
}
