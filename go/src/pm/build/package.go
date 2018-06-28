// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"

	"golang.org/x/crypto/ed25519"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/merkle"
	"fuchsia.googlesource.com/pm/pkg"
)

// Init initializes package metadata in the output directory. A manifest
// is generated with a name matching the output directory name.
func Init(cfg *Config) error {
	pkgName := cfg.PkgName
	if pkgName == "" {
		pkgName = filepath.Base(cfg.OutputDir)
		if pkgName == "." {
			var err error
			pkgName, err = filepath.Abs(pkgName)
			if err != nil {
				return fmt.Errorf("build: unable to compute package name from directory: %s", err)
			}
			pkgName = filepath.Base(pkgName)
		}
	}
	metadir := filepath.Join(cfg.OutputDir, "meta")
	if err := os.MkdirAll(metadir, os.ModePerm); err != nil {
		return err
	}

	meta := filepath.Join(metadir, "package")
	if _, err := os.Stat(meta); os.IsNotExist(err) {
		f, err := os.Create(meta)
		if err != nil {
			return err
		}

		p := pkg.Package{
			Name:    pkgName,
			Version: "0",
		}

		err = json.NewEncoder(f).Encode(&p)
		f.Close()
		if err != nil {
			return err
		}
	}
	return nil
}

// Update walks the contents of the package and updates the merkle root values
// within the contents file. Update is typically executed before signing.
func Update(cfg *Config) error {
	metadir := filepath.Join(cfg.OutputDir, "meta")
	os.MkdirAll(metadir, os.ModePerm)

	manifest, err := cfg.Manifest()
	if err != nil {
		return err
	}

	contentsPath := filepath.Join(metadir, "contents")
	pkgContents := manifest.Content()

	// manifestLines is a channel containing unpacked manifest paths
	var manifestLines = make(chan struct{ src, dest string }, len(pkgContents))
	for dest, src := range pkgContents {
		manifestLines <- struct{ src, dest string }{src, dest}
	}
	close(manifestLines)

	// contentCollector receives "contents" lines to added to contentsPath
	var contentCollector = make(chan string, len(pkgContents))
	var errors = make(chan error)

	// w is a group that is done when contentCollector is fully populated
	var w sync.WaitGroup
	for i := runtime.NumCPU(); i > 0; i-- {
		w.Add(1)

		go func() {
			defer w.Done()

			for in := range manifestLines {
				var t merkle.Tree
				cf, err := os.Open(in.src)
				if err != nil {
					errors <- fmt.Errorf("build.Update: open %s for %s: %s", in.src, in.dest, err)
					return
				}
				_, err = t.ReadFrom(bufio.NewReader(cf))
				cf.Close()
				if err != nil {
					errors <- err
					return
				}

				contentCollector <- fmt.Sprintf("%s=%x", in.dest, t.Root())
			}
		}()
	}

	// done proxies the waitgroup completion so it is selectable
	var done = make(chan struct{})
	go func() {
		w.Wait()
		done <- struct{}{}
	}()

	select {
	case <-done:
		// if we're done, close contentCollector so we can iterate it's buffer
		close(contentCollector)
	case err := <-errors:
		// exit on the first error
		return err
	}

	var contentLines = []string{}
	for line := range contentCollector {
		contentLines = append(contentLines, line+"\n")
	}
	sort.Strings(contentLines)

	manifest.Paths["meta/contents"] = contentsPath

	return ioutil.WriteFile(contentsPath,
		[]byte(strings.Join(contentLines, "")), os.ModePerm)
}

// Sign creates a pubkey and signature file in the meta directory of the given
// package, using the given private key. The generated signature is computed
// using EdDSA, and includes as a message all files from meta except for any
// pre-existing signature. The resulting signature is written to
// packageDir/meta/signature.
func Sign(cfg *Config) error {
	pubKeyPath := filepath.Join(cfg.OutputDir, "meta", "pubkey")

	manifest, err := cfg.Manifest()
	if err != nil {
		return err
	}

	p, err := manifest.Package()
	if err != nil {
		return err
	}
	if err := p.Validate(); err != nil {
		return err
	}

	pkey, err := cfg.PrivateKey()
	if err != nil {
		return err
	}

	if err := ioutil.WriteFile(pubKeyPath, pkey.Public().(ed25519.PublicKey), os.ModePerm); err != nil {
		return err
	}
	manifest.Paths["meta/pubkey"] = pubKeyPath

	msg, err := signingMessage(manifest)
	if err != nil {
		return err
	}

	sig := ed25519.Sign(pkey, msg)

	sigPath := filepath.Join(cfg.OutputDir, "meta", "signature")

	if err := ioutil.WriteFile(sigPath, sig, os.ModePerm); err != nil {
		return err
	}
	manifest.Paths["meta/signature"] = sigPath
	return nil
}

// ErrVerificationFailed indicates that a package failed to verify
var ErrVerificationFailed = errors.New("package verification failed")

// Verify ensures that packageDir/meta/signature is a valid EdDSA signature of
// meta/* by the public key in meta/pubkey
func Verify(cfg *Config) error {
	manifest, err := cfg.Manifest()
	if err != nil {
		return err
	}

	pubkey, err := ioutil.ReadFile(manifest.Paths["meta/pubkey"])
	if err != nil {
		return err
	}

	sig, err := ioutil.ReadFile(manifest.Paths["meta/signature"])
	if err != nil {
		return err
	}

	msg, err := signingMessage(manifest)
	if err != nil {
		return err
	}

	if !ed25519.Verify(pubkey, msg, sig) {
		return ErrVerificationFailed
	}

	return nil
}

// signingMessage generates the message that will be signed / verified.
func signingMessage(manifest *Manifest) ([]byte, error) {
	var msg []byte
	signingFiles := manifest.SigningFiles()
	for _, name := range signingFiles {
		msg = append(msg, name...)
	}
	for _, name := range signingFiles {
		buf, err := ioutil.ReadFile(manifest.Paths[name])
		if err != nil {
			return nil, err
		}

		msg = append(msg, buf...)
	}
	return msg, nil
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
var RequiredFiles = []string{"meta/contents", "meta/signature", "meta/pubkey", "meta/package"}

// Validate ensures that the package contains the required files and that it has a verified signature.
func Validate(cfg *Config) error {
	manifest, err := cfg.Manifest()
	if err != nil {
		return err
	}
	meta := manifest.Meta()

	for _, f := range RequiredFiles {
		if _, ok := meta[f]; !ok {
			return ErrRequiredFileMissing{f}
		}
	}

	return Verify(cfg)
}

// Seal archives meta/ into a FAR archive named meta.far.
func Seal(cfg *Config) (string, error) {
	manifest, err := cfg.Manifest()
	if err != nil {
		return "", err
	}

	if err := Validate(cfg); err != nil {
		return "", err
	}

	archive, err := os.Create(cfg.MetaFAR())
	if err != nil {
		return "", err
	}

	if err := far.Write(archive, manifest.Meta()); err != nil {
		return "", err
	}

	if _, err := archive.Seek(0, io.SeekStart); err != nil {
		return "", err
	}

	var tree merkle.Tree
	if _, err := tree.ReadFrom(archive); err != nil {
		return "", err
	}
	if err := ioutil.WriteFile(cfg.MetaFARMerkle(), []byte(fmt.Sprintf("%x", tree.Root())), os.ModePerm); err != nil {
		return "", err
	}
	return cfg.MetaFAR(), archive.Close()
}
