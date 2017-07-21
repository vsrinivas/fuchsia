// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path"
	"path/filepath"
	"time"

	"fuchsia.googlesource.com/pm/merkle"
	tuf "github.com/flynn/go-tuf"
)

var fuchsiaBuildDir = os.Getenv("FUCHSIA_BUILD_DIR")

const serverBase = "amber-files"

var (
	usage = "usage: publish (-p|-b) [-k=<keys_dir>] [-n=<name>] [-r=<repo_path>] -f=file "
	// TODO(jmatt) support publishing batches of files instead of just singles
	tufFile  = flag.Bool("p", false, "Publish a package.")
	regFile  = flag.Bool("b", false, "Publish a content blob.")
	filePath = flag.String("f", "", "Path of the file to publish")
	name     = flag.String("n", "", "Name/path used for the published file. This only applies to '-p', package files If not supplied, the relative path supplied to '-f' will be used.")
	repoPath = flag.String("r", filepath.Join(os.Getenv("FUCHSIA_BUILD_DIR"), serverBase), "Path to the TUF repository directory.")
	keySrc   = flag.String("k", fuchsiaBuildDir, "Directory containing the signing keys.")

	keySet       = []string{"timestamp.json", "targets.json", "snapshot.json"}
	rootJSONName = "root_manifest.json"
)

type ErrFileAddFailed string

func (e ErrFileAddFailed) Error() string {
	return fmt.Sprintf("amber: file couldn't be added: %s", string(e))
}

func NewAddErr(m string, e error) ErrFileAddFailed {
	return ErrFileAddFailed(fmt.Sprintf("%s: %v", e))
}

func main() {
	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}
	flag.Parse()

	if *repoPath == serverBase {
		log.Fatal("Either set $FUCHSIA_BUILD_DIR or supply a path with -r.")
		return
	}

	if !*tufFile && !*regFile {
		log.Fatal("File must be published as either a regular or verified file!")
		return
	}

	if *tufFile && *regFile {
		log.Fatal("File can not be both regular and verified")
		return
	}

	if _, e := os.Stat(*filePath); e != nil {
		log.Fatal("File path must be valid")
		return
	}

	if e := os.MkdirAll(*repoPath, os.ModePerm); e != nil {
		log.Fatalf("Repository path %q does not exist and could not be created.\n",
			*repoPath)
	}

	repo, err := initRepo(*repoPath, *keySrc)
	if err != nil {
		log.Fatalf("Error initializing repo: %v\n", err)
		return
	}

	if *tufFile {
		if name == nil || len(*name) == 0 {
			name = filePath
		}
		if err = addTUFFile(repo, *repoPath, *filePath, *name); err != nil {
			log.Fatalf("Problem adding signed file file: %v\n", err)
		}
	} else {
		if name != nil && len(*name) > 0 {
			log.Fatal("Name is not a valid argument for content addressed files")
			return
		}
		if name, err := addRegFile(*filePath, *repoPath); err != nil {
			log.Fatal("Error adding regular file: %v\n", err)
		} else {
			fmt.Printf("Added file as %s\n", name)
		}
	}
}

func initRepo(r string, k string) (*tuf.Repo, error) {
	if info, e := os.Stat(r); e != nil || !info.IsDir() {
		return nil, os.ErrInvalid
	}

	keysDir := filepath.Join(r, "keys")
	if e := os.MkdirAll(keysDir, 0777); e != nil {
		return nil, e
	}

	if e := populateKeys(keysDir, k); e != nil {
		return nil, e
	}

	if e := copyFile(filepath.Join(r, "repository", "root.json"), filepath.Join(k, rootJSONName)); e != nil {
		fmt.Println("Failed to copy root manifest")
		return nil, e
	}

	s := tuf.FileSystemStore(r, func(role string, confirm bool) ([]byte, error) { return []byte(""), nil })
	repo, e := tuf.NewRepo(s, "sha512")
	if e != nil {
		return nil, e
	}

	return repo, nil
}

func addTUFFile(repo *tuf.Repo, rPath string, fPath string, name string) error {
	stagingPath := filepath.Join(rPath, "staged", "targets", name)

	if err := copyFile(stagingPath, fPath); err != nil {
		return NewAddErr("copying file to staging directory failed", err)
	}
	if err := createTUFMeta(stagingPath, name, repo); err != nil {
		return NewAddErr("problem creating TUF metadata", err)
	}
	if err := repo.Snapshot(tuf.CompressionTypeNone); err != nil {
		return NewAddErr("problem snapshotting repository", err)
	}
	if err := repo.TimestampWithExpires(time.Now().AddDate(0, 0, 7)); err != nil {
		return NewAddErr("problem timestamping repository", err)
	}
	if err := repo.Commit(); err != nil {
		return NewAddErr("problem committing repository changes", err)
	}

	return nil
}

func addRegFile(fPath string, rPath string) (string, error) {
	root, err := computeMerkle(fPath)
	if err != nil {
		return "", err
	}

	rootStr := hex.EncodeToString(root)
	return rootStr, copyFile(filepath.Join(rPath, "repository", "blobs", rootStr), fPath)
}

func createTUFMeta(path string, name string, repo *tuf.Repo) error {
	// compute merkle root
	root, err := computeMerkle(path)
	if err != nil {
		return err
	}

	// add merkle root as custom JSON
	jsonStr := fmt.Sprintf("{\"merkle\":\"%x\"}", root)
	json := json.RawMessage(jsonStr)

	// add file with custom JSON to repository
	return repo.AddTarget(name, json)
}

func populateKeys(destPath string, srcPath string) error {
	for _, k := range keySet {

		if err := copyFile(filepath.Join(destPath, k), filepath.Join(srcPath, k)); err != nil {
			return err
		}
	}

	return nil
}

func copyFile(dst string, src string) error {
	info, err := os.Stat(src)
	if err != nil {
		return err
	}

	if !info.Mode().IsRegular() {
		return os.ErrInvalid
	}

	os.MkdirAll(path.Dir(dst), 0777)

	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)

	return err
}

func computeMerkle(path string) ([]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	mTree := &merkle.Tree{}

	if _, err = mTree.ReadFrom(f); err != nil {
		return nil, err
	}
	return mTree.Root(), nil
}
