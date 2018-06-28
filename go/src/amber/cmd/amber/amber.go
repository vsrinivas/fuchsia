// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"hash"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strings"
	"time"

	"amber/daemon"
	"amber/ipcserver"
	"amber/pkg"
	"amber/source"

	amber_fidl "fidl/fuchsia/amber"

	"app/context"
	"syscall/zx"
)

const (
	defaultSourceDir = "/system/data/amber/sources"
)

var (
	// TODO(jmatt) replace hard-coded values with something better/more flexible
	usage      = "usage: amber [-k=<path>] [-s=<path>] [-u=<url>]"
	store      = flag.String("s", "/data/amber/store", "The path to the local file store")
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

	// The source dir is where we store our database of sources. Because we
	// don't currently have a mechanism to run "post-install" scripts,
	// we'll use the existence of the data dir to signify if we need to
	// load in the default sources.
	storeExists, err := exists(*store)
	if err != nil {
		log.Fatal(err)
	}

	d, err := startupDaemon(*store)
	if err != nil {
		log.Fatalf("failed to start daemon: %s", err)
	}
	defer d.CancelAll()

	// Now that the daemon is up and running, we can register all of the
	// system configured sources.
	//
	// TODO(etryzelaar): Since these sources are only installed once,
	// there's currently no way to upgrade them. PKG-82 is tracking coming
	// up with a plan to address this.
	if !storeExists {
		log.Printf("initializing store: %s", *store)

		if err := addDefaultSourceConfigs(d, defaultSourceDir); err != nil {
			log.Fatalf("failed to register default sources: %s", err)
		}
	}

	supMon := daemon.NewSystemUpdateMonitor(d, *autoUpdate)
	go func(s *daemon.SystemUpdateMonitor) {
		s.Start()
		log.Println("system update monitor exited")
	}(supMon)

	startFIDLSvr(d, supMon)

	//block forever
	select {}
}

// LoadSourceConfigs install source configs from a directory.  The directory
// structure looks like:
//
//     $dir/source1/config.json
//     $dir/source2/config.json
//     ...
func addDefaultSourceConfigs(d *daemon.Daemon, dir string) error {
	files, err := ioutil.ReadDir(dir)
	if err != nil {
		return err
	}

	for _, file := range files {
		p := filepath.Join(dir, file.Name(), "config.json")
		log.Printf("loading source config %s", p)

		cfg, err := loadSourceConfig(p)
		if err != nil {
			return err
		}

		if err := d.AddTUFSource(cfg); err != nil {
			return err
		}
	}

	return nil
}

func loadSourceConfig(path string) (*amber_fidl.SourceConfig, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var cfg amber_fidl.SourceConfig
	if err := json.NewDecoder(f).Decode(&cfg); err != nil {
		return nil, err
	}

	return &cfg, nil
}

func startFIDLSvr(d *daemon.Daemon, s *daemon.SystemUpdateMonitor) {
	cxt := context.CreateFromStartupInfo()
	apiSrvr := ipcserver.NewControlSrvr(d, s)
	cxt.OutgoingService.AddService(amber_fidl.ControlName, func(c zx.Channel) error {
		return apiSrvr.Bind(c)
	})
	cxt.Serve()
}

func startupDaemon(store string) (*daemon.Daemon, error) {
	d, err := daemon.NewDaemon(store, pkg.NewPackageSet(), daemon.ProcessPackage, []source.Source{})
	if err != nil {
		return nil, err
	}

	log.Println("monitoring for updates")

	return d, err
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

// Check if a path exists.
func exists(path string) (bool, error) {
	if _, err := os.Stat(path); err != nil {
		if os.IsNotExist(err) {
			return false, nil
		} else {
			return false, err
		}
	}

	return true, nil
}
