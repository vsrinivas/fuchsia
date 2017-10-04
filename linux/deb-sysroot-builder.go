// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generates a Debian based sysroot.
package main

import (
	"bytes"
	"bufio"
	"compress/gzip"
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"golang.org/x/crypto/openpgp"
	"gopkg.in/yaml.v2"
)

const (
	aptRepo = "http://http.us.debian.org/debian"
)

type stringsValue []string

func (i *stringsValue) String() string {
	return strings.Join(*i, ",")
}

func (i *stringsValue) Set(value string) error {
    *i = strings.Split(value, ",")
    return nil
}

var (
	release string
	components stringsValue
	arch string
	packagesFile string
	outDir string
	packageList string
	debsCache string
	keyRingFile string
)

func init() {
	components = stringsValue{"main"}

	flag.StringVar(&release, "release", "stretch", "Debian release to use")
	flag.Var(&components, "c", "Debian components to use")
	flag.StringVar(&arch, "arch", "amd64", "Target architecture")
	flag.StringVar(&packagesFile, "packages", "packages.yml", "List of packages")
	flag.StringVar(&outDir, "out", "sysroot", "Output directory")
	flag.StringVar(&packageList, "list", "packagelist", "Package list filename")
	flag.StringVar(&debsCache, "cache", "debs", "Cache for .deb files")

	// gpg keyring file generated using:
	//   export KEYS="46925553 2B90D010 518E17E1 1A7B6500"
	//   gpg --keyserver keys.gnupg.net --recv-keys $KEYS
	//   gpg --output ./debian-archive-stretch-stable.gpg --export $KEYS
	flag.StringVar(&keyRingFile, "keyring", "debian-archive-stretch-stable.gpg", "Keyring file")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "usage: deb-sysroot-builder\n")
		flag.PrintDefaults()
	}
}

type pkg struct {
	name string
	hash string
}

type pkgs []pkg

func (p pkgs) Len() int {
	return len(p)
}

func (p pkgs) Swap(i, j int) {
	p[i], p[j] = p[j], p[i]
}

func (p pkgs) Less(i, j int) bool {
	return p[i].name < p[j].name
}

func downloadPackageList() ([]pkg, error) {
	pkgs := map[string]pkg{}

	file, err := os.Open(keyRingFile)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	keyring, err := openpgp.ReadKeyRing(file)
	if err != nil {
		return nil, err
	}

	for _, dist := range []string{release, release + "-updates"} {
		u, err := url.Parse(aptRepo)
		u.Path = path.Join(u.Path, "dists", dist, "Release")
		r, err := http.Get(u.String())
		if err != nil {
			return nil, err
		}
		defer r.Body.Close()

		b, err := ioutil.ReadAll(r.Body)
		if err != nil {
			return nil, err
		}

		u, err = url.Parse(aptRepo)
		u.Path = path.Join(u.Path, "dists", dist, "Release.gpg")
		r, err = http.Get(u.String())
		if err != nil {
			return nil, err
		}
		defer r.Body.Close()

		_, err = openpgp.CheckArmoredDetachedSignature(keyring, bytes.NewReader(b), r.Body)
		if err != nil {
			return nil, err
		}

		for _, c := range components {
			u, err := url.Parse(aptRepo)
			u.Path = path.Join(u.Path, "dists", dist, c, "binary-" + arch, "Packages.gz")
			r, err := http.Get(u.String())
			if err != nil {
				return nil, err
			}
			defer r.Body.Close()

			buf, err := ioutil.ReadAll(r.Body)
			if err != nil {
				return nil, err
			}

			f := path.Join(c, "binary-" + arch, "Packages.gz")
			l := regexp.MustCompile(`([0-9a-z]{64})\s+\d+\s+` + f)
			m := l.FindStringSubmatch(string(b))

			sum := sha256.Sum256(buf)
			if m[1] != hex.EncodeToString(sum[:]) {
				return nil, fmt.Errorf("%s: checksum doesn't match", f)
			}

			g, err := gzip.NewReader(bytes.NewReader(buf))
			if err != nil {
				return nil, err
			}
			d, err := ioutil.ReadAll(g)
			if err != nil {
				return nil, err
			}

			sep := regexp.MustCompile(`\n\n`)
			for _, p := range sep.Split(string(d), -1) {
				rec := regexp.MustCompile(`(?m)^(?:Package:|Filename:|SHA256:) (.*)$`)
				m := rec.FindAllStringSubmatch(p, 3)
				if m != nil {
					pkgs[m[0][1]] = pkg{ m[1][1], m[2][1] }
				}
			}
		}
	}

	f, err := ioutil.ReadFile(packagesFile)
	if err != nil {
		return nil, err
	}
    var packages map[string][]string
	if err := yaml.Unmarshal(f, &packages); err != nil {
        return nil, err
    }

	list := []pkg{}
	for _, name := range append(packages["all"], packages[arch]...) {
		if pkg, ok := pkgs[name]; ok {
			list = append(list, pkg)
		} else {
			fmt.Printf("Package %s not found\n", name)
		}
	}

	return list, nil
}

func relativize(link, target, dir string, patterns []string) error {
	for _, p := range patterns {
		matches, err := filepath.Glob(filepath.Join(dir, p))
		if err != nil {
			return err
		}
		for _, m := range matches {
			if link == m {
				if err := os.Remove(link); err != nil {
					return err
				}
				relDir := ".." + strings.Repeat("/..", strings.Count(p, "/") - 1)
				if err := os.Symlink(relDir + target, link); err != nil {
					return err
				}
				return nil
			}
		}
	}
	return nil
}

func installSysroot(list []pkg, installDir string) error {
	if err := os.MkdirAll(debsCache, 0777); err != nil {
		return err
	}

	if err := os.RemoveAll(installDir); err != nil {
		return err
	}

	// This is only needed when running dpkg-shlibdeps.
	if err := os.MkdirAll(filepath.Join(installDir, "debian"), 0777); err != nil {
		return err
	}

	// An empty control file is necessary to run dpkg-shlibdeps.
	if file, err := os.OpenFile(filepath.Join(installDir, "debian", "control"), os.O_RDONLY|os.O_CREATE, 0644); err != nil {
		return err
	} else {
		file.Close()
	}

	for _, pkg := range list {
		filename := filepath.Base(pkg.name)
		deb := filepath.Join(debsCache, filename)
		if _, err := os.Stat(deb); os.IsNotExist(err) {
			fmt.Printf("Downloading %s...\n", filename)

			u, err := url.Parse(aptRepo)
			u.Path = path.Join(u.Path, pkg.name)
			r, err := http.Get(u.String())
			if err != nil {
				return err
			}
			defer r.Body.Close()

			buf, err := ioutil.ReadAll(r.Body)
			if err != nil {
				return err
			}

			sum := sha256.Sum256(buf)
			if pkg.hash != hex.EncodeToString(sum[:]) {
				return fmt.Errorf("%s: checksum doesn't match", filename)
			}

			if err := ioutil.WriteFile(deb, buf, 0644); err != nil {
				return err
			}
		}

		fmt.Printf("Installing %s...\n", filename)
		// Extract the content of the package into the install directory.
		err := exec.Command("dpkg-deb", "-x", deb, installDir).Run()
		if err != nil {
			return err
		}
		// Get the package name.
		cmd := exec.Command("dpkg-deb", "--field", deb, "Package")
		stdout, err := cmd.StdoutPipe()
		if err != nil {
			return err
		}
		if err := cmd.Start(); err != nil {
			return err
		}
		r := bufio.NewReader(stdout)
		baseDir, _, err := r.ReadLine()
		if err != nil {
			return err
		}
		if err := cmd.Wait(); err != nil {
			return err
		}
		// Construct the path which contains the control information files.
		controlDir := filepath.Join(installDir, "debian", string(baseDir), "DEBIAN")
		if err := os.MkdirAll(controlDir, 0777); err != nil {
			return err
		}
		// Extract the control information files.
		err = exec.Command("dpkg-deb", "-e", deb, controlDir).Run()
		if err != nil {
			return err
		}
	}

	// Prune /usr/share, leave only pkgconfig files.
	files, err := ioutil.ReadDir(filepath.Join(installDir, "usr", "share"))
	if err != nil {
		return err
	}
	for _, file := range files {
		if file.Name() != "pkgconfig" {
			if err := os.RemoveAll(filepath.Join(installDir, "usr", "share", file.Name())); err != nil {
				return err
			}
		}
	}

	// Relativize all symlinks within the sysroot.
	for _, d := range []string{"usr/lib", "lib64", "lib"} {
		p := filepath.Join(installDir, d)
		if _, err := os.Stat(p); os.IsNotExist(err) {
			continue
		}
		if err := filepath.Walk(p, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}
			if info.Mode() & os.ModeSymlink == os.ModeSymlink {
				target, err := os.Readlink(path)
				if err != nil {
					return err
				}
				if !filepath.IsAbs(target) {
					return nil
				}
				patterns := []string{
					"usr/lib/gcc/*-linux-gnu/*/*",
					"usr/lib/*-linux-gnu/*",
					"usr/lib/*",
					"lib64/*",
					"lib/*",
				}
				if err := relativize(path, target, installDir, patterns); err != nil {
					return err
				}
				if _, err := os.Stat(path); os.IsNotExist(err) {
					return fmt.Errorf("%s: broken link", path)
				}
			}
			return nil
		}); err != nil {
			return err
		}
	}

	// Rewrite and relativize all linkerscripts.
	linkerscripts := []string{
        "usr/lib/*-linux-gnu/libpthread.so",
        "usr/lib/*-linux-gnu/libc.so",
	}
	for _, l := range linkerscripts {
		matches, err := filepath.Glob(filepath.Join(installDir, l))
		if err != nil {
			return err
		}
		for _, path := range matches {
			read, err := ioutil.ReadFile(path)
			if err != nil {
				return err
			}
			sub:= regexp.MustCompile(`(/usr)?/lib/[a-z0-9_]+-linux-gnu/`)
			contents := sub.ReplaceAllString(string(read), "")
			if err := ioutil.WriteFile(path, []byte(contents), 0644); err != nil {
				return err
			}
		}
	}

	if err := os.RemoveAll(filepath.Join(installDir, "debian")); err != nil {
		return err
	}

	return nil
}

func main() {
	flag.Parse()

	if _, err := os.Stat(keyRingFile); os.IsNotExist(err) {
		log.Fatalf("keyring file '%s' missing", keyRingFile)
	}

	list, err := downloadPackageList()
	if err != nil {
		log.Fatal(err)
	}
	sort.Sort(pkgs(list))

	if packageList != "" {
		f, err := os.Create(packageList)
		if err != nil {
			log.Fatal(err)
		}
		defer f.Close()
		w := bufio.NewWriter(f)
		for _, pkg := range list {
			fmt.Fprintf(w, "%s\n", strings.TrimPrefix(pkg.name, "pool/"))
		}
		w.Flush()
	}

	if err := installSysroot(list, outDir); err != nil {
		log.Fatal(err)
	}
}
