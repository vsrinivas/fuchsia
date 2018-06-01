// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"crypto/sha512"
	"encoding/hex"
	"flag"
	"fmt"
	"hash"
	"io"
	"log"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"time"

	"amber/daemon"
	"amber/ipcserver"
	"amber/pkg"
	"amber/source"

	amber_fidl "fidl/amber"

	"app/context"
	"syscall/zx"

	tuf_data "github.com/flynn/go-tuf/data"
)

const lhIP = "http://127.0.0.1"
const port = 8083

var (
	// TODO(jmatt) replace hard-coded values with something better/more flexible
	usage      = "usage: amber [-k=<path>] [-s=<path>] [-u=<url>]"
	store      = flag.String("s", "/data/amber/tuf", "The path to the local file store")
	addr       = flag.String("u", fmt.Sprintf("%s:%d", lhIP, port), "The URL (including port if not using port 80)  of the update server.")
	keys       = flag.String("k", "/pkg/data/keys", "Path to use to initialize the client's keys. This is only needed the first time the command is run.")
	delay      = flag.Duration("d", 0*time.Second, "Set a delay before Amber does its work")
	autoUpdate = flag.Bool("a", false, "Automatically update and restart the system as updates become available")

	needsPath = "/pkgfs/needs"
)

func main() {
	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}

	log.SetPrefix("amber: ")
	log.SetFlags(log.Ltime)

	readExtraFlags()

	flag.Parse()
	time.Sleep(*delay)

	srvUrl, err := url.Parse(*addr)
	if err != nil {
		log.Fatalf("bad address for update server %s", err)
	}

	keys, err := source.LoadKeys(*keys)
	if err != nil {
		log.Fatalf("loading root keys failed %s", err)
	}

	d, err := startupDaemon(srvUrl, *store, keys)
	if err != nil {
		log.Fatalf("failed to start daemon: %s", err)
	}
	defer d.CancelAll()

	supMon := daemon.NewSystemUpdateMonitor(d, *autoUpdate)
	go func(s *daemon.SystemUpdateMonitor) {
		s.Start()
		log.Println("system update monitor exited")
	}(supMon)

	startFIDLSvr(d, supMon)

	//block forever
	select {}
}

func startFIDLSvr(d *daemon.Daemon, s *daemon.SystemUpdateMonitor) {
	cxt := context.CreateFromStartupInfo()
	apiSrvr := ipcserver.NewControlSrvr(d, s)
	cxt.OutgoingService.AddService(amber_fidl.ControlName, func(c zx.Channel) error {
		return apiSrvr.Bind(c)
	})
	cxt.Serve()
}

func startupDaemon(srvURL *url.URL, store string, keys []*tuf_data.Key) (*daemon.Daemon, error) {

	reqSet := newPackageSet([]string{"/pkg/bin/app"})

	checker := daemon.NewDaemon(store, reqSet, daemon.ProcessPackage, []source.Source{})

	// TODO(etryzelaar) temporary workaround until we transition to loading
	// from a config file.
	keycfgs := make([]amber_fidl.KeyConfig, len(keys))
	for i, key := range keys {
		keycfgs[i] = amber_fidl.KeyConfig{
			Type:  key.Type,
			Value: hex.EncodeToString(key.Value.Public),
		}
	}

	cfg := amber_fidl.SourceConfig{
		RepoUrl:    srvURL.String(),
		RootKeys:   keycfgs,
		RatePeriod: 0,
		RateLimit:  0,
	}
	if err := checker.AddTUFSource(&cfg); err != nil {
		return nil, err
	}

	blobURL := *srvURL
	blobURL.Path = filepath.Join(blobURL.Path, "blobs")
	checker.AddBlobRepo(daemon.BlobRepo{Address: blobURL.String(), Interval: time.Second * 5})

	log.Println("monitoring for updates")
	return checker, nil
}

func newPackageSet(files []string) *pkg.PackageSet {
	reqSet := pkg.NewPackageSet()

	d := sha512.New()
	// get the current SHA512 hash of the file
	for _, name := range files {
		sha, err := digest(name, d)
		d.Reset()
		if err != nil {
			continue
		}

		hexStr := hex.EncodeToString(sha)
		pkg := pkg.Package{Name: name, Version: hexStr}
		reqSet.Add(&pkg)
	}

	return reqSet
}

func digest(name string, hash hash.Hash) ([]byte, error) {
	f, e := os.Open(name)
	if e != nil {
		fmt.Printf("couldn't open file to fingerprint %s\n", e)
		return nil, e
	}
	defer f.Close()

	if _, err := io.Copy(hash, f); err != nil {
		fmt.Printf("file digest failed %s\n", err)
		return nil, e
	}
	return hash.Sum(nil), nil
}

var flagsDir = filepath.Join("/system", "data", "amber", "flags")

func readExtraFlags() {
	d, err := os.Open(flagsDir)
	if err != nil {
		if !os.IsNotExist(err) {
			log.Println("unexpected error reading %q: %s", flagsDir, err)
		}
		return
	}
	defer d.Close()

	files, err := d.Readdir(0)
	if err != nil {
		log.Printf("error listing flags directory %s", err)
		return
	}
	for _, f := range files {
		if f.IsDir() || f.Size() == 0 {
			continue
		}

		fPath := filepath.Join(d.Name(), f.Name())
		file, err := os.Open(fPath)
		if err != nil {
			log.Printf("flags file %q could not be opened: %s", fPath, err)
			continue
		}
		r := bufio.NewReader(file)
		for {
			line, err := r.ReadString('\n')
			if err != nil && err != io.EOF {
				log.Printf("flags file %q had read error: %s", fPath, err)
				break
			}

			line = strings.TrimSpace(line)
			os.Args = append(os.Args, line)
			if err == io.EOF {
				break
			}
		}
		file.Close()
	}
}
