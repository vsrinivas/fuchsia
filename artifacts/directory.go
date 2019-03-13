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

// The default name of a test's output file. We only store a single file containing a
// test's combined stdout and stderr streams today.  This will change in the future.
const DefaultTestOutputName = "stdout-stderr.txt"

// BuildDirectory represents a Fuchsia CI build's artifact directory. Refer to the
// layout in doc.go for the layout of this directory. When amending the layout, prefer
// adding convenience methods on this type to encourages all clients to create objects
// within this BuildDirectory using the same layout.
type BuildDirectory struct {
	*directory
}

// NewTestOutputObject creates a new ObjectHandle to hold the output of the given test
// execution in this BuildDirectory. testName is the name of the test, envName is the
// canonical name of the test environment. Both are normalized according to
// normalizePathSegment.
func (d BuildDirectory) NewTestOutputObject(ctx context.Context, testName, envName string) *storage.ObjectHandle {
	return d.cd("tests").cd(testName).cd(envName).Object(DefaultTestOutputName)
}

// directory is a handle to a Cloud Storage "directory". It provides a minimal
// filesystem-like interface for a Cloud Storage object hierarchy where "/" is used as the
// path separator. Any methods added to this struct are forward to other directory types.
type directory struct {
	bucket *storage.BucketHandle
	root   string
}

// Object returns a handle to the given object within this directory. path is the path to
// the object relative to this directory.
func (d *directory) Object(path string) *storage.ObjectHandle {
	path = fmt.Sprintf("%s/%s", d.root, path)
	return d.bucket.Object(path)
}

// List lists all of the objects in this directory.
func (d *directory) List(ctx context.Context) ([]string, error) {
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

// CD returns a handle to some child directory of this directory. directChild should be a
// direct child of the current directory, not a grandchild or any entry deeper in the
// tree. The child's name is normalized according to normalizePathSegment, so using a
// nested path may result in an unexpected file tree.
func (d *directory) cd(directChild string) *directory {
	return &directory{
		bucket: d.bucket,
		root:   fmt.Sprintf("%s/%s", d.root, normalizePathSegment(directChild)),
	}
}
