// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package pkg contains the in memory representations of a Package in the pm
// system and associated utilities.
package pkg

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"

	"fuchsia.googlesource.com/pm/far"
	"fuchsia.googlesource.com/pm/merkle"

	"golang.org/x/crypto/ed25519"
)

// Package is a representation of basic package metadata
type Package struct {
	Name    string `json:"name"`
	Version string `json:"version"`
}

// Init initializes package metadata in the given package directory. A manifest
// is generated with a name matching the directory name. A content manifest is
// also created including all files found in the package directory.
func Init(packageDir string) error {
	pkgName := filepath.Base(packageDir)
	metadir := filepath.Join(packageDir, "meta")
	os.MkdirAll(metadir, os.ModePerm)

	meta := filepath.Join(metadir, "package.json")
	if _, err := os.Stat(meta); os.IsNotExist(err) {
		f, err := os.Create(meta)
		if err != nil {
			return err
		}

		p := Package{
			Name:    pkgName,
			Version: "0",
		}

		err = json.NewEncoder(f).Encode(&p)
		f.Close()
		if err != nil {
			return err
		}
	}

	f, err := os.Create(filepath.Join(metadir, "contents"))
	if err != nil {
		return err
	}
	defer f.Close()

	return WalkContents(packageDir, func(path string) error {
		_, err := fmt.Fprintln(f, path)
		return err
	})
}

// Update walks the contents of the package and updates the merkle root values
// within the contents file. Update is typically executed before signing.
func Update(packageDir string) error {
	metadir := filepath.Join(packageDir, "meta")
	os.MkdirAll(metadir, os.ModePerm)

	f, err := os.Create(filepath.Join(metadir, "contents"))
	if err != nil {
		return err
	}
	defer f.Close()

	// TODO(raggi): instead of recreating the contents manifest with all found
	// files, just read the file and update the merkle roots for files in the
	// manifest
	return WalkContents(packageDir, func(path string) error {
		var t merkle.Tree
		cf, err := os.Open(filepath.Join(packageDir, path))
		if err != nil {
			return err
		}
		defer cf.Close()

		_, err = t.ReadFrom(cf)
		if err != nil {
			return err
		}

		_, err = fmt.Fprintf(f, "%s:%x\n", path, t.Root())
		if err != nil {
			return err
		}

		return nil
	})
}

// Sign creates a pubkey and signature file in the meta directory of the given
// package, using the given private key. The generated signature is computed
// using EdDSA, and includes as a message all files from meta except for any
// pre-existing signature. The resulting signature is written to
// packageDir/meta/signature.
func Sign(packageDir string, privateKey ed25519.PrivateKey) error {
	signatureFile := filepath.Join(packageDir, "meta", "signature")

	if err := ioutil.WriteFile(filepath.Join(packageDir, "meta", "pubkey"), privateKey.Public().(ed25519.PublicKey), os.ModePerm); err != nil {
		return err
	}

	// NOTE: cannot use WalkContents as it is critical that the contents file
	// is signed. It is also important that we establish a deterministic order for
	// the signature.
	metas, err := filepath.Glob(filepath.Join(packageDir, "meta", "*"))
	if err != nil {
		return err
	}
	sort.Strings(metas)
	metaFiles := []string{}
	for _, path := range metas {
		if path == signatureFile {
			continue
		}
		metaFiles = append(metaFiles, path)
	}

	var msg []byte
	for _, f := range metaFiles {
		buf, err := ioutil.ReadFile(f)
		if err != nil {
			return err
		}
		msg = append(msg, buf...)
	}

	sig := ed25519.Sign(privateKey, msg)

	if err := ioutil.WriteFile(signatureFile, sig, os.ModePerm); err != nil {
		return err
	}

	return nil
}

// ErrVerificationFailed indicates that a package failed to verify
var ErrVerificationFailed = errors.New("package verification failed")

// Verify ensures that packageDir/meta/signature is a valid EdDSA signature of
// meta/* by the public key in meta/pubkey
func Verify(packageDir string) error {

	signatureFile := filepath.Join(packageDir, "meta", "signature")

	pubkey, err := ioutil.ReadFile(filepath.Join(packageDir, "meta", "pubkey"))
	if err != nil {
		return err
	}

	sig, err := ioutil.ReadFile(signatureFile)
	if err != nil {
		return err
	}

	metas, err := filepath.Glob(filepath.Join(packageDir, "meta", "*"))
	if err != nil {
		return err
	}
	sort.Strings(metas)
	var msg []byte
	for _, path := range metas {
		if path == signatureFile {
			continue
		}
		buf, err := ioutil.ReadFile(path)
		if err != nil {
			return err
		}

		msg = append(msg, buf...)
	}

	if !ed25519.Verify(pubkey, msg, sig) {
		return ErrVerificationFailed
	}

	return nil
}

// ErrRequiredFileMissing is returned by operations when the operation depends
// on a file that was not found on disk.
type ErrRequiredFileMissing struct {
	Path string
}

func (e ErrRequiredFileMissing) Error() string {
	return fmt.Sprintf("pkg: missing required file: %q", e.Path)
}

// RequiredFiles is a list of files that are required before a package can be sealed.
var RequiredFiles = []string{"meta/contents", "meta/signature", "meta/pubkey", "meta/package.json"}

// Validate ensures that the package contains the required files and that it has a verified signature.
func Validate(packageDir string) error {
	metaFiles, err := filepath.Glob(filepath.Join(packageDir, "meta", "*"))
	if err != nil {
		return err
	}

	// metaMap is the manifest for far to use to construct the archive, as well as
	// a convenient way to look up if the required files are present
	metaMap := map[string]string{}
	for _, f := range metaFiles {
		rel, err := filepath.Rel(packageDir, f)
		if err != nil {
			return err
		}
		metaMap[rel] = f
	}

	for _, f := range RequiredFiles {
		if _, ok := metaMap[f]; !ok {
			return ErrRequiredFileMissing{f}
		}
	}

	return Verify(packageDir)
}

// Seal archives meta/ into a FAR archive named meta.far.
func Seal(packageDir string) error {

	if err := Validate(packageDir); err != nil {
		return err
	}

	archive, err := os.Create(filepath.Join(packageDir, "meta.far"))
	if err != nil {
		return err
	}

	metaFiles, err := filepath.Glob(filepath.Join(packageDir, "meta", "*"))
	if err != nil {
		return err
	}

	// metaMap is the manifest for far to use to construct the archive, as well as
	// a convenient way to look up if the required files are present
	metaMap := map[string]string{}
	for _, f := range metaFiles {
		rel, err := filepath.Rel(packageDir, f)
		if err != nil {
			return err
		}
		metaMap[rel] = f
	}

	far.Write(archive, metaMap)
	return archive.Close()
}

// WalkContents is like a filepath.Walk in a package dir, but with a simplified
// interface. It skips over the meta/signature and meta/contents files, which
// are not able to be included in the contents file itself.
func WalkContents(d string, f func(path string) error) error {
	return filepath.Walk(d, func(abspath string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		path, err := filepath.Rel(d, abspath)
		if err != nil {
			return err
		}

		// TODO(raggi): needs some kind of ignorefile/config
		switch path {
		case "meta/signature", "meta/contents":
			return nil
		case ".git", ".jiri":
			return filepath.SkipDir
		}
		if info.IsDir() {
			return nil
		}

		return f(path)
	})
}
