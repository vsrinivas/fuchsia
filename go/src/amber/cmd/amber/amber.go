// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"flag"
	"fmt"
	"hash"
	"io"
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
	"syslog/logger"
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

	ctx := context.CreateFromStartupInfo()
	registerLogger(ctx)

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

	startFIDLSvr(ctx, d, supMon)

	//block forever
	select {}
}

type logWriter struct{}

func (l *logWriter) Write(data []byte) (n int, err error) {
	// Strip out the trailing newline the `log` library adds because the
	// logging service also adds a trailing newline.
	if len(data) > 0 && data[len(data)-1] == '\n' {
		data = data[:len(data)-1]
	}

	if err := logger.Infof("%s", data); err != nil {
		return 0, err
	}

	return len(data), nil
}

func registerLogger(ctx *context.Context) {
	if err := logger.InitDefaultLoggerWithTags(ctx.GetConnector(), "amber"); err != nil {
		log.Printf("error initializing syslog interface: %s", err)
	}
	log.SetOutput(&logWriter{})
}

// addDefaultSourceConfigs installs source configs from a directory.
// The directory structure looks like:
//
//     $dir/source1/config.json
//     $dir/source2/config.json
//     ...
func addDefaultSourceConfigs(d *daemon.Daemon, dir string) error {
	configs, err := source.LoadSourceConfigs(dir)
	if err != nil {
		return err
	}

	for _, cfg := range configs {
		if err := d.AddTUFSource(cfg); err != nil {
			return err
		}
	}

	return nil
}

func startFIDLSvr(ctx *context.Context, d *daemon.Daemon, s *daemon.SystemUpdateMonitor) {
	apiSrvr := ipcserver.NewControlSrvr(d, s)
	ctx.OutgoingService.AddService(amber_fidl.ControlName, func(c zx.Channel) error {
		return apiSrvr.Bind(c)
	})
	ctx.Serve()
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
