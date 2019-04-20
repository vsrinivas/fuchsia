// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This package manages the SystemUpdateMonitor, which polls for changes to the
// update/0 package. When a change is observed, it triggers a system update.

package sys_update

import (
	"bufio"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"amber/atonce"
	"amber/daemon"
	"amber/metrics"

	"app/context"
	"fidl/fuchsia/sys"
)

const (
	devmgrConfigPath     = "/boot/config/devmgr"
	devmgrConfigPkgfsCmd = "zircon.system.pkgfs.cmd"
	packagesDir          = "/pkgfs/packages"

	// TODO: support configuration of update target
	updateName    = "update"
	updateVersion = "0"
)

var (
	timerDur       = time.Hour
	pkgsvrCmdRegex = regexp.MustCompile("^bin/pkgsvr\\+([0-9a-f]{64})$")
)

type SystemUpdateMonitor struct {
	d                       *daemon.Daemon
	updateMerkle            string
	systemImageMerkle       string
	latestSystemImageMerkle string
	auto                    bool
	ticker                  *time.Ticker
}

type ErrNoPackage struct {
	name string
}

func (e ErrNoPackage) Error() string {
	return fmt.Sprintf("no package named %q is available on the system", e.name)
}

func NewErrNoPackage(name string) ErrNoPackage {
	return ErrNoPackage{name: name}
}

func NewSystemUpdateMonitor(a bool, d *daemon.Daemon) *SystemUpdateMonitor {
	return &SystemUpdateMonitor{
		auto:   a,
		d:      d,
		ticker: time.NewTicker(timerDur),
	}
}

func (upMon *SystemUpdateMonitor) Check(initiator metrics.Initiator) error {
	return atonce.Do("system-update-monitor", "", func() error {
		start := time.Now()

		// If we haven't computed the initial "update" package's
		// "meta/contents" merkle, do it now.
		if upMon.updateMerkle == "" {
			updateMerkle, err := getUpdateMerkle()
			if err == nil {
				upMon.updateMerkle = updateMerkle
			} else {
				if _, ok := err.(ErrNoPackage); ok {
					log.Printf("sys_upd_mon: \"update\" package not present on system")
				} else {
					log.Printf("sys_upd_mon: error computing \"update\" package's "+
						"\"meta/contents\" merkle. Treating package as "+
						"nonexistent: %s", err)
				}
			}
		}

		// Likewise, try to determine the system's active
		// "system_image" package.
		if upMon.systemImageMerkle == "" {
			systemImageMerkle, err := currentSystemImageMerkle()
			if err != nil {
				log.Printf("sys_upd_mon: error determining \"system_image\"'s merkle: %s", err)
			} else {
				log.Printf("sys_upd_mon: current \"system_image\" merkle: %q", systemImageMerkle)
				upMon.systemImageMerkle = systemImageMerkle
			}
		}

		// Update all sources
		upMon.d.Update()

		// Get the latest merkle root for the update package
		root, length, err := upMon.d.MerkleFor(updateName, updateVersion, "")
		if err != nil {
			log.Printf("sys_upd_mon: unable to resolve new update package: %s", err)
			return err
		}

		err = upMon.d.GetPkg(root, length)
		if err != nil {
			log.Printf("sys_upd_mon: unable to fetch package update: %s", err)
			return err
		}

		// Now that we've fetched the "update" package, extract out
		// it's "meta/contents" merkle.
		latestUpdateMerkle, err := getUpdateMerkle()
		if err != nil {
			log.Printf("sys_upd_mon: error computing \"update\" package's "+
				"\"meta/contents\" merkle: %s", err)
			return err
		}

		if upMon.needsUpdate(latestUpdateMerkle) {
			log.Println("Performing GC")
			upMon.d.GC()

			log.Println("System update starting...")
			metrics.Log(metrics.OtaStart{
				Initiator: initiator,
				Source:    upMon.systemImageMerkle,
				Target:    upMon.latestSystemImageMerkle,
				When:      start,
			})

			launchDesc := sys.LaunchInfo{
				Url: "fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx",
				Arguments: &[]string{
					fmt.Sprintf("-initiator=%s", initiator),
					fmt.Sprintf("-start=%d", start.UnixNano()),
					fmt.Sprintf("-source=%s", upMon.systemImageMerkle),
					fmt.Sprintf("-target=%s", upMon.latestSystemImageMerkle),
				},
			}
			if err := runProgram(launchDesc); err != nil {
				log.Printf("sys_upd_mon: updater: %s", err)
			}
		} else {
			log.Println("sys_upd_mon: no newer system version available")

			metrics.Log(metrics.SystemUpToDate{
				Initiator: initiator,
				Version:   upMon.systemImageMerkle,
				When:      start,
			})
		}

		upMon.updateMerkle = latestUpdateMerkle
		return nil
	})
}

func (upMon *SystemUpdateMonitor) Start() {
	if !upMon.auto {
		return
	}

	log.Printf("sys_upd_mon: monitoring for updates every %s", timerDur)
	go func() {
		for range upMon.ticker.C {
			upMon.Check(metrics.InitiatorAutomatic)
		}
	}()
}

func (upMon *SystemUpdateMonitor) Stop() {
	upMon.ticker.Stop()
}

// Check if we need to do an update.
func (upMon *SystemUpdateMonitor) needsUpdate(latestUpdateMerkle string) bool {
	latestSystemImageMerkle, err := latestSystemImageMerkle()
	if err == nil {
		upMon.latestSystemImageMerkle = latestSystemImageMerkle
	}

	// It's possible for updates to fail. For example, we could lose
	// network connection while downloading the latest packages as
	// specified in the `update` package. So, we need a way to tell if the
	// system is up to date. Unfortunately there's no easy way to check at
	// the moment, because there's no way to tell if the update package
	// matches what is actively running (This needs PKG-216, which adds a
	// sysconfig partition that will contain this information). So, we need
	// to inspect the local system to see if one has been applied. This
	// might not be 100% accurate, but it should help make the OTA process
	// more reliable.
	//
	// The first check is the easiest one. Earlier, we saved the update
	// package's merkle when amber started. Then we compare it to the
	// current update package, which we just downloaded. If they changed,
	// then we know we need to do an update. This however will only work
	// once since we don't wait for the `system_updater` to complete before
	// updating our local reference. We could do that, but this check could
	// still fail if `system_updater` fails and amber is restarted.
	//
	// NOTE: An easy way to force this check is to remove the update
	// package from the dynamic index:
	//
	//     rm -r /data/pkgfs_index/packages/update
	//
	if upMon.updateMerkle != latestUpdateMerkle {
		log.Printf("sys_upd_mon: \"update\" package's \"meta/contents\" merkle has changed, triggering update")
		return true
	} else {
		log.Printf("sys_upd_mon: \"update\" package \"meta/contents\" merkle has not changed")
	}

	// If we made it to this point, we've already seen this update package
	// package, but the system still might have an outstanding update.
	// While we can't tell if the update package has been applied, we can
	// however tell what `system_image` package is active.  The
	// `/boot/config/devmgr` contains the `system_image` package merkle.
	// We can compare it to the merkle found in the `update` package's
	// `package` manifest.
	//
	// The `system_image` describes all of the userspace artifacts, and so
	// if we can see it changed, we can tell if the vast majority of
	// Fuchsia is up to date or not. Unfortunately this can't tell us if
	// the kernel is up to date.  All is not lost though, because the
	// `system_image` package contains a file `data/build/snapshot`. This
	// file contains the Jiri git repository revisions from the build that
	// produced this `system_image`. Most importantly, this includes the
	// Zircon commit. It's possible for a developer to explicitly build a
	// kernel of a separate commit (which won't cause the snapshot file to
	// change), but this check should do a decent job of catching a Zircon
	// change for people not directly modifying Zircon.

	if err != nil {
		log.Printf("sys_upd_mon: error parsing \"update\"'s package \"meta/contents\": %s", err)
	} else {
		log.Printf("sys_upd_mon: latest system_image merkle: %q", latestSystemImageMerkle)

		// If the "/boot/config/devmgr" file format changed, we might
		// not be able to parse out the "system_image" merkle, which
		// would leave this variable blank. In that case, we ignore
		// this check because we don't want to keep triggering an OTA.
		if upMon.systemImageMerkle == "" {
			log.Printf("sys_upd_mon: no current \"system_image\" package merkle, skipping check")
		} else if upMon.systemImageMerkle != latestSystemImageMerkle {
			log.Printf("sys_upd_mon: \"system_image\" merkle changed, triggering update")
			return true
		} else {
			log.Printf("sys_upd_mon: \"system_image\" merkle has not changed")
		}
	}

	return false
}

func runProgram(info sys.LaunchInfo) error {
	context := context.CreateFromStartupInfo()
	req, pxy, err := sys.NewLauncherInterfaceRequest()
	if err != nil {
		return fmt.Errorf("could not make launcher request object: %s", err)
	}
	context.ConnectToEnvService(req)
	ch := req.ToChannel()
	defer ch.Close()
	contReq, proc, err := sys.NewComponentControllerInterfaceRequest()
	defer proc.Close()
	if err != nil {
		return fmt.Errorf("error creating component controller request: %s", err)
	}

	err = pxy.CreateComponent(info, contReq)
	if err != nil {
		return fmt.Errorf("error starting system updater: %s", err)
	}

	returnCode, terminationReason, err := proc.ExpectOnTerminated()
	if err != nil {
		return fmt.Errorf("error waiting for system updater to complete: %s", err)
	} else if terminationReason == sys.TerminationReasonExited {
		if returnCode != 0 {
			return fmt.Errorf("system updater exited with code: %d", returnCode)
		}
	} else {
		return fmt.Errorf("system updater exited with reason: %d", uint32(terminationReason))
	}

	return nil
}

func readManifest(path string) (map[string]string, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	m := make(map[string]string)
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.SplitN(scanner.Text(), "=", 2)
		if len(parts) != 2 {
			log.Printf("manifest %q line is malformed: %q", path, line)
		} else {
			m[parts[0]] = parts[1]
		}
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return m, nil
}

func currentSystemImageMerkle() (string, error) {
	m, err := readManifest(devmgrConfigPath)
	if err != nil {
		return "", err
	}

	if cmd, ok := m[devmgrConfigPkgfsCmd]; ok {
		if res := pkgsvrCmdRegex.FindStringSubmatch(cmd); res != nil {
			return res[1], nil
		} else {
			return "", fmt.Errorf("could not parse pkgsvr command: %s", cmd)
		}
	} else {
		return "", fmt.Errorf("%q config does not contain %q key", devmgrConfigPath, devmgrConfigPkgfsCmd)
	}
}

func latestSystemImageMerkle() (string, error) {
	path := filepath.Join(packageDir("update", updateVersion), "packages")

	m, err := readManifest(path)
	if err != nil {
		return "", err
	}

	if merkle, ok := m[fmt.Sprintf("system_image/%s", updateVersion)]; ok {
		return merkle, nil
	} else {
		return "", fmt.Errorf("system_image package not in %q", path)
	}
}

func packageDir(name, version string) string {
	return filepath.Join(packagesDir, name, version)
}

func getUpdateMerkle() (string, error) {
	b, err := ioutil.ReadFile(filepath.Join(packageDir(updateName, updateVersion), "meta"))
	return string(b), err
}
