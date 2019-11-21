// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generates a Debian based sysroot.
package main

import (
	"bufio"
	"bytes"
	"compress/gzip"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"

	"github.com/go-yaml/yaml"
	"github.com/google/subcommands"
	"golang.org/x/crypto/openpgp"
)

// gpg keyring file generated using:
//   export KEYS="46925553 2B90D010 518E17E1 1A7B6500"
//   gpg --keyserver keys.gnupg.net --recv-keys $KEYS
//   gpg --output ./debian-archive-stretch-stable.gpg --export $KEYS

const (
	// http://http.us.debian.org/debian no longer contains arm64 packages
	// for jessie so we instead use the last snapshot that still has them.
	aptRepo = "https://snapshot.debian.org/archive/debian/20190324T093412Z"
)

type stringsValue []string

func (i *stringsValue) String() string {
	return strings.Join(*i, ",")
}

func (i *stringsValue) Set(value string) error {
	*i = strings.Split(value, ",")
	return nil
}

type Config struct {
	Dists         []string  `yaml:"dists"`
	Components    []string  `yaml:"components"`
	Keyring       string    `yaml:"keyring"`
	Architectures []string  `yaml:"architectures"`
	Packages      []Package `yaml:"packages"`
}

type Package struct {
	Name          string   `yaml:"package"`
	Architectures []string `yaml:"architectures,omitempty"`
}

type Lockfile struct {
	Hash     string    `yaml:"hash"`
	Updated  time.Time `yaml:"updated"`
	Packages []Lock    `yaml:"packages"`
}

type Lock struct {
	Name     string `yaml:"package"`
	Version  string `yaml:"version"`
	Filename string `yaml:"filename"`
	Hash     string `yaml:"hash"`
}

type Locks []Lock

func (l Locks) Len() int {
	return len(l)
}

func (l Locks) Swap(i, j int) {
	l[i], l[j] = l[j], l[i]
}

func (l Locks) Less(i, j int) bool {
	if l[i].Name == l[j].Name {
		return l[i].Filename < l[j].Filename
	}
	return l[i].Name < l[j].Name
}

// Descriptor represents a Debian package description.
type Descriptor map[string]string

// parsePackages parses Debian's control file which described packages.
//
// See chapter 5.1 (Syntax of control files) of the Debian Policy Manual:
// http://www.debian.org/doc/debian-policy/ch-controlfields.html
func parsePackages(r io.Reader) ([]Descriptor, error) {
	// Packages are separated by double newline, use scanner to split them.
	scanner := bufio.NewScanner(r)
	scanner.Split(func(data []byte, atEOF bool) (int, []byte, error) {
		const separator = "\n\n"
		if i := bytes.Index(data, []byte(separator)); i != -1 {
			return i + len(separator), data[:i], nil
		}
		return 0, nil, nil
	})
	space := regexp.MustCompile(`\s+`)
	exp := regexp.MustCompile(`(?m)^(?P<key>\S+): (?P<value>(.*)(?:$\s^ .*)*)$`)
	var descriptors []Descriptor
	for scanner.Scan() {
		descriptor := make(Descriptor)
		for _, m := range exp.FindAllStringSubmatch(scanner.Text(), -1) {
			descriptor[m[1]] = space.ReplaceAllString(m[2], " ")
		}
		descriptors = append(descriptors, descriptor)
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}
	return descriptors, nil
}

func downloadPackageList(config *Config, depends bool) ([]Lock, error) {
	descriptors := map[string]map[string]Descriptor{}

	file, err := os.Open(config.Keyring)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	keyring, err := openpgp.ReadKeyRing(file)
	if err != nil {
		return nil, err
	}

	for _, dist := range config.Dists {
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
		var lines []string
		sha256section := false
		for _, l := range strings.Split(string(b), "\n") {
			if sha256section {
				if strings.HasPrefix(l, " ") {
					lines = append(lines, l[1:])
				} else {
					sha256section = false
				}
			} else if strings.HasPrefix(l, "SHA256:") {
				sha256section = true
			}
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

		for _, a := range config.Architectures {
			for _, c := range config.Components {
				u, err := url.Parse(aptRepo)
				u.Path = path.Join(u.Path, "dists", dist, c, "binary-"+a, "Packages.gz")
				r, err := http.Get(u.String())
				if err != nil {
					return nil, err
				}
				defer r.Body.Close()

				buf, err := ioutil.ReadAll(r.Body)
				if err != nil {
					return nil, err
				}

				var checksum string
				f := path.Join(c, "binary-"+a, "Packages.gz")
				for _, l := range lines {
					if strings.HasSuffix(l, f) {
						checksum = strings.Fields(l)[0]
						break
					}
				}
				if checksum == "" {
					return nil, fmt.Errorf("%s: checksum missing", f)
				}

				sum := sha256.Sum256(buf)
				if checksum != hex.EncodeToString(sum[:]) {
					return nil, fmt.Errorf("%s: checksum doesn't match", f)
				}

				g, err := gzip.NewReader(bytes.NewReader(buf))
				if err != nil {
					return nil, err
				}

				ps, err := parsePackages(g)
				if err != nil {
					return nil, err
				}

				// We only want development libraries, filter out everything else.
				for _, p := range ps {
					// Use sections as a coarse grained filter.
					var section bool
					switch p["Section"] {
					case "libs", "libdevel", "devel", "x11":
						section = true
					}
					// Use tags as a more fine-grained filter.
					var tag bool
					if section {
						for _, n := range strings.Split(p["Tag"], ", ") {
							t := strings.Split(strings.TrimSpace(n), " ")[0]
							switch t {
							case "devel::library", "x11::library", "role::devel-lib", "role::shared-lib":
								tag = true
							}
						}
					}
					// Skip everything that doesn't match.
					if section && tag {
						n := p["Package"]
						if _, ok := descriptors[n]; !ok {
							descriptors[n] = map[string]Descriptor{}
						}
						descriptors[n][a] = p
					}
				}
			}
		}
	}

	type dependency struct {
		name         string
		architecture string
	}

	// Place the initial set of packages into queue.
	var queue []dependency
	for _, p := range config.Packages {
		if len(p.Architectures) > 0 {
			for _, a := range p.Architectures {
				queue = append(queue, dependency{
					name:         p.Name,
					architecture: a,
				})
			}
		} else {
			for _, a := range config.Architectures {
				queue = append(queue, dependency{
					name:         p.Name,
					architecture: a,
				})
			}
		}
	}

	// Process all dependencies until we drain the queue.
	locks := map[string]map[string]Lock{}
	for len(queue) > 0 {
		p := queue[0]
		queue = queue[1:]
		if lock, ok := locks[p.name]; ok {
			if _, ok := lock[p.architecture]; ok {
				continue
			}
		}
		if ds, ok := descriptors[p.name]; ok {
			if _, ok := locks[p.name]; !ok {
				locks[p.name] = map[string]Lock{}
			}
			if pkg, ok := ds[p.architecture]; ok {
				locks[p.name][p.architecture] = Lock{
					Name:     pkg["Package"],
					Version:  pkg["Version"],
					Filename: pkg["Filename"],
					Hash:     pkg["SHA256"],
				}
				if depends {
					for _, n := range strings.Split(pkg["Depends"], ", ") {
						d := strings.Split(strings.TrimSpace(n), " ")[0]
						queue = append(queue, dependency{
							name:         d,
							architecture: p.architecture,
						})
					}
				}
			} else {
				return nil, fmt.Errorf("package %q not found for architecture %q", p.name, p.architecture)
			}
		} else {
			return nil, fmt.Errorf("package %q not found", p.name)
		}
	}

	// Eliminate all duplicates.
	hashes := map[string]Lock{}
	for _, l := range locks {
		for _, p := range l {
			hashes[p.Hash] = p
		}
	}

	// Flatten into a list.
	var list []Lock
	for _, p := range hashes {
		list = append(list, p)
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
				relDir := ".." + strings.Repeat("/..", strings.Count(p, "/")-1)
				if err := os.Symlink(relDir+target, link); err != nil {
					return err
				}
				return nil
			}
		}
	}
	return nil
}

func installSysroot(list []Lock, installDir, debsCache string) error {
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
		filename := filepath.Base(pkg.Filename)
		deb := filepath.Join(debsCache, filename)
		if _, err := os.Stat(deb); os.IsNotExist(err) {
			fmt.Printf("Downloading %s...\n", filename)

			u, err := url.Parse(aptRepo)
			u.Path = path.Join(u.Path, pkg.Filename)
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
			if pkg.Hash != hex.EncodeToString(sum[:]) {
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

	// Ensure that we don't have duplicate file names that only differ in case.
	type rename struct{ oldpath, newpath string }
	renames := []rename{}

	for _, d := range []string{"usr/include/linux"} {
		p := filepath.Join(installDir, d)
		if _, err := os.Stat(p); os.IsNotExist(err) {
			continue
		}
		paths := make(map[string][]string)
		if err := filepath.Walk(p, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}
			if info.Mode().IsRegular() && filepath.Ext(path) == ".h" {
				name := strings.ToLower(path)
				if _, ok := paths[name]; !ok {
					paths[name] = []string{}
				}
				paths[name] = append(paths[name], path)
			}
			return nil
		}); err != nil {
			return err
		}

		for _, ps := range paths {
			if len(ps) > 1 {
				sort.Sort(sort.Reverse(sort.StringSlice(ps)))
				for i, p := range ps {
					if i > 0 {
						ext := filepath.Ext(p)
						renames = append(renames, rename{p, p[:len(p)-len(ext)] + strings.Repeat("_", i) + ext})
					}
				}
			}
		}
	}

	for _, r := range renames {
		fmt.Printf("Renaming %s to %s\n", r.oldpath, r.newpath)
		if err := os.Rename(r.oldpath, r.newpath); err != nil {
			return err
		}
	}

	for _, d := range []string{"usr/include/linux"} {
		p := filepath.Join(installDir, d)
		if _, err := os.Stat(p); os.IsNotExist(err) {
			continue
		}
		if err := filepath.Walk(p, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}
			update := false
			if info.Mode().IsRegular() && filepath.Ext(path) == ".h" {
				content, err := ioutil.ReadFile(path)
				if err != nil {
					return err
				}
				for _, r := range renames {
					oldbase := filepath.Base(r.oldpath)
					newbase := filepath.Base(r.newpath)
					if strings.Contains(string(content), oldbase) {
						content = bytes.Replace(content, []byte(oldbase), []byte(newbase), 1)
						update = true
					}
				}
				if update {
					fmt.Printf("Updating %s...\n", path)
					if err := ioutil.WriteFile(path, content, info.Mode()); err != nil {
						return err
					}
				}
			}
			return nil
		}); err != nil {
			return err
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
			if info.Mode()&os.ModeSymlink == os.ModeSymlink {
				target, err := os.Readlink(path)
				if err != nil {
					return err
				}
				if !filepath.IsAbs(target) {
					return nil
				}
				patterns := []string{
					"usr/lib/gcc/*-linux-gnu*/*/*",
					"usr/lib/*-linux-gnu*/*",
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
		"usr/lib/*-linux-gnu*/libpthread.so",
		"usr/lib/*-linux-gnu*/libc.so",
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
			sub := regexp.MustCompile(`(/usr)?/lib/[a-z0-9_]+-linux-gnu[a-z0-9]*/`)
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

type updateCmd struct {
	config   string
	lockfile string
	depends  bool
}

func (*updateCmd) Name() string     { return "update" }
func (*updateCmd) Synopsis() string { return "Update the lock file." }
func (*updateCmd) Usage() string {
	return `update [-config] [-lock] [-depends]:
	Update the lock file to include specific package versions.
`
}

func (c *updateCmd) SetFlags(f *flag.FlagSet) {
	f.StringVar(&c.config, "config", "packages.yml", "Package configuration")
	f.StringVar(&c.lockfile, "lock", "packages.lock", "Lockfile filename")
	f.BoolVar(&c.depends, "depends", false, "Transitively include dependencies")
}

func (c *updateCmd) Execute(_ context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	d, err := ioutil.ReadFile(c.config)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to read config: %v\n", err)
		return subcommands.ExitFailure
	}
	config := &Config{}
	if err := yaml.Unmarshal(d, &config); err != nil {
		fmt.Fprintf(os.Stderr, "failed to unmarshal config: %v\n", err)
		return subcommands.ExitFailure
	}

	if _, err := os.Stat(config.Keyring); os.IsNotExist(err) {
		fmt.Fprintf(os.Stderr, "keyring file '%s' missing\n", config.Keyring)
		return subcommands.ExitUsageError
	}

	list, err := downloadPackageList(config, c.depends)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to download package list: %v\n", err)
		return subcommands.ExitFailure
	}
	sort.Sort(Locks(list))

	hash := sha256.New()
	hash.Write(d)

	lockfile := Lockfile{
		Updated:  time.Now(),
		Hash:     fmt.Sprintf("%x", hash.Sum(nil)),
		Packages: list,
	}

	l, err := yaml.Marshal(&lockfile)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to marshal lockfile: %v\n", err)
		return subcommands.ExitFailure
	}

	if err := ioutil.WriteFile(c.lockfile, l, 0666); err != nil {
		fmt.Fprintf(os.Stderr, "failed to write lockfile: %v\n", err)
		return subcommands.ExitFailure
	}

	return subcommands.ExitSuccess
}

type installCmd struct {
	outDir    string
	debsCache string
}

func (*installCmd) Name() string     { return "install" }
func (*installCmd) Synopsis() string { return "Install packages" }
func (*installCmd) Usage() string {
	return `install [-out] [-cache] <lockfile>:
  Install the specific versions from the lock file.
`
}

func (c *installCmd) SetFlags(f *flag.FlagSet) {
	f.StringVar(&c.outDir, "out", "sysroot", "Output directory")
	f.StringVar(&c.debsCache, "cache", "debs", "Cache for .deb files")
}

func (c *installCmd) Execute(_ context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if len(f.Args()) != 1 {
		fmt.Fprintln(os.Stderr, "missing lockfile argument")
		return subcommands.ExitUsageError
	}
	lockfile := f.Args()[0]

	d, err := ioutil.ReadFile(lockfile)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to read lockfile: %v\n", err)
		return subcommands.ExitFailure
	}
	var lock Lockfile
	if err := yaml.Unmarshal(d, &lock); err != nil {
		fmt.Fprintf(os.Stderr, "failed to unmarshal lockfile: %v\n", err)
		return subcommands.ExitFailure
	}

	if err := installSysroot(lock.Packages, c.outDir, c.debsCache); err != nil {
		fmt.Fprintf(os.Stderr, "failed to install sysroot: %v\n", err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(&updateCmd{}, "")
	subcommands.Register(&installCmd{}, "")

	flag.Parse()
	ctx := context.Background()
	os.Exit(int(subcommands.Execute(ctx)))
}
