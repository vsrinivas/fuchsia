// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package expand implements the `pm expand` command
package expand

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"sync"
	"sync/atomic"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/merkle"
	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/pkg"
)

const metaFar = "meta.far"

const usage = `Usage: %s expand <archive>
expand a single .far representation of a package into a repository
`

var merklePat = regexp.MustCompile("^[0-9a-f]{64}$")

// Run reads an archive given in flags.Arg(1) (the first argument after
// `expand`) and unpacks the archive, adding it's contents to the appropriate
// locations in the output directory to install the package.
func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("expand", flag.ExitOnError)

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	af, err := os.Open(fs.Arg(0))
	if err != nil {
		return err
	}
	defer af.Close()

	pkgArchive, err := far.NewReader(af)
	if err != nil {
		return err
	}

	// Make sure the path is absolute so it doesn't matter where the
	// consumers of the package are run from.
	outputDir, err := filepath.Abs(filepath.Clean(cfg.OutputDir))
	if err != nil {
		return err
	}

	if err := writeMetadataAndManifest(pkgArchive, outputDir); err != nil {
		return err
	}

	return writeBlobs(pkgArchive, outputDir)
}

func merkleFor(b []byte) (build.MerkleRoot, error) {
	var res build.MerkleRoot

	var tree merkle.Tree
	if _, err := tree.ReadFrom(bytes.NewReader(b)); err != nil {
		return res, err
	}

	copy(res[:], tree.Root())

	return res, nil
}

// Extract the meta.far to the `outputDir`, and write package manifests.
// `$outputDir/package.manifest` contains:
//     $PKG_NAME.$PKG_VERSION=$outputDir/meta.far
// `package_manifest.json` contains a package output manifest as built by `pm
// build -outut-package-manifest`.
func writeMetadataAndManifest(pkgArchive *far.Reader, outputDir string) error {
	// First, extract the package info from the archive, or error out if
	// the meta.far is malformed.
	pkgMetaBytes, err := pkgArchive.ReadFile(metaFar)
	if err != nil {
		return err
	}

	pkgMetaMerkle, err := merkleFor(pkgMetaBytes)
	if err != nil {
		return err
	}

	pkgMeta, err := far.NewReader(bytes.NewReader(pkgMetaBytes))
	if err != nil {
		return err
	}

	p, err := readMetaPackage(pkgMeta)
	if err != nil {
		return err
	}

	contents, err := readMetaContents(pkgMeta)
	if err != nil {
		return err
	}

	blobs := make([]build.PackageBlobInfo, 0, len(contents)+1)

	blobs = append(blobs, build.PackageBlobInfo{
		SourcePath: filepath.Join(outputDir, metaFar),
		Path:       "meta/",
		Merkle:     pkgMetaMerkle,
		Size:       uint64(len(pkgMetaBytes)),
	})

	for path, merkle := range contents {
		blobs = append(blobs, build.PackageBlobInfo{
			SourcePath: filepath.Join(outputDir, "blobs", merkle.String()),
			Path:       path,
			Merkle:     merkle,
			Size:       pkgArchive.GetSize(merkle.String()),
		})
	}

	if err := os.MkdirAll(outputDir, os.ModePerm); err != nil {
		return err
	}

	// Write meta.far
	if err := ioutil.WriteFile(filepath.Join(outputDir, metaFar), pkgMetaBytes, 0666); err != nil {
		return err
	}

	// Write meta.far.merkle
	if err := ioutil.WriteFile(filepath.Join(outputDir, metaFar+".merkle"), []byte(pkgMetaMerkle.String()), 0666); err != nil {
		return err
	}

	// Write blobs.json
	f, err := os.Create(filepath.Join(outputDir, "blobs.json"))
	if err != nil {
		return err
	}

	encoder := json.NewEncoder(f)
	encoder.SetIndent("", "    ")
	if err := encoder.Encode(blobs); err != nil {
		f.Close()
		return err
	}
	f.Close()

	// Write out package_manifest.json
	pkgManifest := build.PackageManifest{
		Version: "1",
		Package: *p,
		Blobs:   blobs,
	}
	content, err := json.MarshalIndent(pkgManifest, "", "    ")
	if err != nil {
		return err
	}
	if err := ioutil.WriteFile(filepath.Join(outputDir, "package_manifest.json"), content, 0644); err != nil {
		return err
	}

	cwd, err := os.Getwd()
	if err != nil {
		return err
	}

	// Write blobs.manifest
	var buf bytes.Buffer
	for _, blob := range blobs {
		relpath, err := filepath.Rel(cwd, blob.SourcePath)
		if err != nil {
			return err
		}
		fmt.Fprintf(&buf, "%s=%s\n", blob.Merkle, relpath)
	}
	if err := ioutil.WriteFile(filepath.Join(outputDir, "blobs.manifest"), buf.Bytes(), 0644); err != nil {
		return err
	}

	// Write package.manifest
	f, err = os.Create(filepath.Join(outputDir, "package.manifest"))
	if err != nil {
		return err
	}
	defer f.Close()

	_, err = fmt.Fprintf(f, "%s/%s=%s\n", p.Name, p.Version, filepath.Join(outputDir, metaFar))
	if err != nil {
		return err
	}

	return nil
}

// Extract the meta-package from the archive.
func readMetaPackage(pkgMeta *far.Reader) (*pkg.Package, error) {
	pkgJSON, err := pkgMeta.ReadFile("meta/package")
	if err != nil {
		return nil, err
	}

	var p pkg.Package
	if err := json.Unmarshal(pkgJSON, &p); err != nil {
		return nil, err
	}

	return &p, nil
}

// Extract the meta-contents from the archive.
func readMetaContents(pkgMeta *far.Reader) (build.MetaContents, error) {
	b, err := pkgMeta.ReadFile("meta/contents")
	if err != nil {
		return nil, err
	}

	return build.ParseMetaContents(bytes.NewReader(b))
}

// Extract out all the blobs into the `outputDir`
func writeBlobs(pkgArchive *far.Reader, outputDir string) error {
	blobDir := filepath.Join(outputDir, "blobs")

	// Extract out the package entries from the archive. Error out if the
	// name is not "meta.far" or a valid merkle root.
	names := []string{}
	for _, name := range pkgArchive.List() {
		if merklePat.Match([]byte(name)) {
			names = append(names, name)
		} else if name != metaFar {
			return fmt.Errorf("package contains invalid name: %q", name)
		}
	}

	if err := os.MkdirAll(blobDir, os.ModePerm); err != nil {
		return err
	}

	// Now, write all the blobs in parallel
	return writeEntries(pkgArchive, blobDir, names)
}

// Extract the specified entries from the .far and write them to the outputDir.
func writeEntries(p *far.Reader, outputDir string, names []string) error {
	// Write out all the entries in parallel to speed things up.
	ch := make(chan string, runtime.NumCPU())

	var w sync.WaitGroup
	var hadError atomic.Value
	hadError.Store(false)
	for i := 0; i < runtime.NumCPU(); i++ {
		w.Add(1)
		go func() {
			defer w.Done()
			for name := range ch {
				if err := writeEntry(p, outputDir, name); err != nil {
					log.Printf("error writing %q: %s", name, err)
					hadError.Store(true)
					return
				}
			}
		}()
	}

	for _, name := range names {
		ch <- name
	}

	close(ch)
	w.Wait()

	if hadError.Load().(bool) {
		return fmt.Errorf("expansion finished with errors")
	}

	return nil
}

// Extract out a specified file from the .far and write it to the outputDir.
func writeEntry(p *far.Reader, outputDir string, name string) error {
	dst := filepath.Join(outputDir, name)
	log.Printf("writing %s to %s", name, dst)

	src, err := p.Open(name)
	if err != nil {
		return err
	}

	f, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer f.Close()

	var pos int64
	var buf [32 * 1024]byte
	for {
		n, err := src.ReadAt(buf[:], pos)
		if n > 0 {
			if _, err := f.Write(buf[:n]); err != nil {
				return err
			}
		}
		pos += int64(n)
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
	}

	return nil
}
