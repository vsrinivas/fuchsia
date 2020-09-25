// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootserver

import (
	"compress/gzip"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/url"
	"os"
	"path/filepath"
	"strings"

	"cloud.google.com/go/storage"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
)

var (
	noOpClose = func() error { return nil }
)

// Image is a fuchsia image as viewed by bootserver; a simplified version of build.Image.
type Image struct {
	// Name is an identifier for this image that usually derives from its target partition.
	// TODO(fxbug.dev/38517): Remove when BootZedbootShim is deprecated.
	Name string
	// Path is the location of the image on disk.
	// TODO(ihuh): Remove when bootserver/cmd/main.go no longer uses it.
	Path string
	// Reader is a reader to the image.
	Reader io.ReaderAt
	// Size is the size of the reader in bytes.
	Size int64
	// Args correspond to the bootserver args that map to this image type.
	Args []string
}

func getImageArgs(img build.Image, bootMode Mode) []string {
	switch bootMode {
	case ModePave:
		return img.PaveArgs
	case ModeNetboot:
		return img.NetbootArgs
	case ModePaveZedboot:
		return img.PaveZedbootArgs
	}
	return nil
}

// ConvertFromBuildImages filters and returns Images corresponding to build Images of a given bootMode.
func ConvertFromBuildImages(buildImages []build.Image, bootMode Mode) ([]Image, func() error, error) {
	var imgs []Image
	closeFunc := noOpClose
	for _, buildImg := range buildImages {
		args := getImageArgs(buildImg, bootMode)
		reader, err := os.Open(buildImg.Path)
		if err != nil {
			if os.IsNotExist(err) {
				// Not all images exist so skip if it doesn't.
				continue
			}
			// Close already opened readers.
			closeImages(imgs)
			return nil, closeFunc, err
		}
		fi, err := reader.Stat()
		if err != nil {
			closeImages(imgs)
			return nil, closeFunc, err
		}
		imgs = append(imgs, Image{
			Name:   buildImg.Type + "_" + buildImg.Name,
			Path:   buildImg.Path,
			Reader: reader,
			Size:   fi.Size(),
			Args:   args,
		})
	}
	closeFunc = func() error {
		return closeImages(imgs)
	}
	return imgs, closeFunc, nil
}

// ImagesFromLocalFS returns Images of a given bootMode that exist on the local
// filesystem.
func ImagesFromLocalFS(manifest string, bootMode Mode) ([]Image, func() error, error) {
	buildImages, err := build.LoadImages(manifest)
	if err != nil {
		return nil, noOpClose, err
	}
	return ConvertFromBuildImages(buildImages, bootMode)
}

// GCSReader is a wrapper around storage.Reader which implements io.ReaderAt.
// TODO(https://github.com/googleapis/google-cloud-go/issues/1686): Use the storage.Reader as is once this is fixed.
type gcsReader struct {
	obj    *storage.ObjectHandle
	reader io.ReadCloser
	index  int64
}

func getUncompressedReader(obj *storage.ObjectHandle) (io.ReadCloser, error) {
	ctx := context.Background()
	objAttrs, err := obj.Attrs(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to get attrs for %q from GCS: %v", obj.ObjectName(), err)
	}
	if objAttrs.ContentEncoding != "gzip" {
		return obj.NewReader(ctx)
	}
	r, err := obj.ReadCompressed(true).NewReader(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to read %q from GCS: %v", obj.ObjectName(), err)
	}
	if r.Attrs.ContentEncoding != "gzip" {
		return nil, fmt.Errorf("content-encoding expected: gzip, actual: %s", r.Attrs.ContentEncoding)
	}

	gr, err := gzip.NewReader(r)
	if err != nil {
		return nil, fmt.Errorf("failed to get gzip reader: %v", err)
	}
	return struct {
		io.Reader
		io.Closer
	}{gr, iomisc.MultiCloser(gr, r)}, nil
}

func (g *gcsReader) ReadAt(buf []byte, offset int64) (int, error) {
	if g.reader == nil || offset < g.index {
		g.Close()
		r, err := getUncompressedReader(g.obj)
		if err != nil {
			return 0, err
		}
		g.reader = r
		g.index = 0
	}
	// If the offset is greater than the index of the reader, we need to read
	// up until the requested offset. These bytes will be ignored as they only
	// need to be read to bring the index up to the offset so that the next
	// calls to Read will read the correct bytes into buf.
	if offset > g.index {
		diff := make([]byte, offset-g.index)
		n, err := io.ReadAtLeast(g.reader, diff, len(diff))
		g.index += int64(n)
		if err != nil && err != io.ErrUnexpectedEOF {
			return 0, err
		}
	}

	n, err := io.ReadAtLeast(g.reader, buf, len(buf))
	g.index += int64(n)
	if err == io.ErrUnexpectedEOF {
		err = io.EOF
	}
	return n, err
}

func (g *gcsReader) Close() error {
	if g.reader != nil {
		return g.reader.Close()
	}
	return nil
}

// ImagesFromGCS returns Images of a given bootMode that exist in GCS. The image
// paths provided in the manifest at the given url are expected to be relative paths
// to the same directory of the manifest.
func ImagesFromGCS(ctx context.Context, manifest *url.URL, bootMode Mode) ([]Image, func() error, error) {
	closeFunc := noOpClose
	bucket := manifest.Host
	client, err := storage.NewClient(ctx)
	if err != nil {
		return nil, closeFunc, err
	}
	bkt := client.Bucket(bucket)
	manifestGcsPath := strings.TrimLeft(manifest.Path, "/")
	obj := bkt.Object(manifestGcsPath)
	r, err := getUncompressedReader(obj)
	if err != nil {
		return nil, closeFunc, fmt.Errorf("failed to get image manifest from GCS: %v", err)
	}
	defer r.Close()
	var buildImgs []build.Image
	if err := json.NewDecoder(r).Decode(&buildImgs); err != nil {
		return nil, closeFunc, fmt.Errorf("failed to decode image manifest: %v", err)
	}
	var imgs []Image
	namespace := filepath.Dir(manifestGcsPath)
	for _, buildImg := range buildImgs {
		args := getImageArgs(buildImg, bootMode)
		obj := bkt.Object(filepath.Join(namespace, buildImg.Path))
		objAttrs, err := obj.Attrs(ctx)
		if err == storage.ErrObjectNotExist {
			// Not all images may have been uploaded so skip if it doesn't exist.
			continue
		} else if err != nil {
			return nil, closeFunc, fmt.Errorf("failed to get object attributes: %v", err)
		}

		imgs = append(imgs, Image{
			Name:   buildImg.Type + "_" + buildImg.Name,
			Reader: &gcsReader{obj: obj},
			Size:   objAttrs.Size,
			Args:   args,
		})
	}

	closeFunc = func() error {
		return closeImages(imgs)
	}
	return imgs, closeFunc, nil
}

// GetImages parses the imageManifest and gets a list of images with readers to
// each image. It returns the images as well as a func to close the image readers.
func GetImages(ctx context.Context, imageManifest string, bootMode Mode) ([]Image, func() error, error) {
	url, err := url.Parse(imageManifest)
	closeFunc := noOpClose
	if err != nil {
		return nil, closeFunc, err
	}
	if url == nil {
		return nil, closeFunc, fmt.Errorf("failed to parse %q", imageManifest)
	}

	var imgs []Image
	if url.Scheme == "gs" {
		imgs, closeFunc, err = ImagesFromGCS(ctx, url, bootMode)
	} else if url.Scheme == "" && url.Host == "" {
		// Assume that this is a filesystem path.
		imgs, closeFunc, err = ImagesFromLocalFS(imageManifest, bootMode)
	} else if url.Scheme != "" && url.Host != "" {
		// TODO(ihuh|joshuaseaton): handle the case of getting images directly over HTTP
		err = fmt.Errorf("unimplemented")
	} else {
		err = fmt.Errorf("unknown manifest reference: %q", imageManifest)
	}
	return imgs, closeFunc, err
}

func closeImages(imgs []Image) error {
	var errs []error
	for _, img := range imgs {
		if img.Reader != nil {
			if closer, ok := img.Reader.(io.Closer); ok {
				if err := closer.Close(); err != nil {
					errs = append(errs, err)
				}
			}
		}
	}
	if len(errs) > 0 {
		return fmt.Errorf("failed to close images: %v", errs)
	}
	return nil
}
