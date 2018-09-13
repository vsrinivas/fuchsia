package daemon

import (
	"amber/pkg"
	"bufio"
	"encoding/hex"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync/atomic"
	"time"

	"app/context"
	"fidl/fuchsia/amber"
	"fidl/fuchsia/sys"
	"syscall/zx"
	"syscall/zx/zxwait"

	"fuchsia.googlesource.com/merkle"
)

var devmgrConfigPath = "/boot/config/devmgr"
var devmgrConfigPkgfsCmd = "zircon.system.pkgfs.cmd"
var packagesDir = filepath.Join("/pkgfs", "packages")
var pkgsvrCmdRegex = regexp.MustCompile("^bin/pkgsvr\\+([0-9a-f]{64})$")

type SystemUpdateMonitor struct {
	halt              uint32
	daemon            *Daemon
	checkNow          chan struct{}
	amber             *amber.ControlInterface
	updateMerkle      string
	systemImageMerkle string

	// if auto is allowed to be reset after instantiation, changes must be made
	// in the run loop to avoid a panic
	auto bool
}

type ErrNoPackage struct {
	name string
}

func (e ErrNoPackage) Error() string {
	return fmt.Sprintf("no package named %s is available on the system", e.name)
}

func NewErrNoPackage(name string) ErrNoPackage {
	return ErrNoPackage{name: name}
}

func NewSystemUpdateMonitor(d *Daemon, a bool) (*SystemUpdateMonitor, error) {
	amber, err := connectToUpdateSrvc()
	if err != nil {
		log.Printf("sys_upd_mon: binding to update service failed: %s", err)
		return nil, err
	}

	return &SystemUpdateMonitor{
		daemon:   d,
		halt:     0,
		checkNow: make(chan struct{}),
		auto:     a,
		amber:    amber,
	}, nil
}

func (upMon *SystemUpdateMonitor) Stop() {
	atomic.StoreUint32(&upMon.halt, 1)
}

func (upMon *SystemUpdateMonitor) Check() {
	if atomic.LoadUint32(&upMon.halt) == 1 {
		return
	}
	upMon.checkNow <- struct{}{}
}

func (upMon *SystemUpdateMonitor) Start() {
	timerDur := time.Hour
	var timer *time.Timer
	if upMon.auto {
		timer = time.NewTimer(timerDur)
	}

	for {
		if upMon.auto {
			select {
			case <-timer.C:
				timer.Reset(timerDur)
			case <-upMon.checkNow:
			}
		} else {
			<-upMon.checkNow
		}

		if atomic.LoadUint32(&upMon.halt) == 1 {
			return
		}

		// If we haven't computed the initial "update" package's
		// "meta/contents" merkle, do it now.
		if upMon.updateMerkle == "" {
			updateMerkle, err := updatePackageContentMerkle()
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

		// FIXME: we need to handle the case where the version number is not 0.
		updatePkg := &pkg.Package{Name: fmt.Sprintf("/update/%d", 0)}
		if err := fetchPackage(updatePkg, upMon.amber); err != nil {
			log.Printf("sys_upd_mon: unable to fetch package update: %s", err)
			continue
		}

		// Now that we've fetched the "update" package, extract out
		// it's "meta/contents" merkle.
		latestUpdateMerkle, err := updatePackageContentMerkle()
		if err != nil {
			log.Printf("sys_upd_mon: error computing \"update\" package's "+
				"\"meta/contents\" merkle: %s", err)
			continue
		}

		if upMon.needsUpdate(latestUpdateMerkle) {
			log.Println("System update starting...")

			launchDesc := sys.LaunchInfo{Url: "system_updater"}
			if err := runProgram(&launchDesc); err != nil {
				log.Printf("sys_upd_mon: updater failed to start: %s", err)
			}
		} else {
			log.Println("sys_upd_mon: no newer system version available")
		}

		upMon.updateMerkle = latestUpdateMerkle
	}
}

// Check if we need to do an update.
func (upMon *SystemUpdateMonitor) needsUpdate(latestUpdateMerkle string) bool {
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

	latestSystemImageMerkle, err := latestSystemImageMerkle()
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

func runProgram(info *sys.LaunchInfo) error {
	context := context.CreateFromStartupInfo()
	req, pxy, err := sys.NewLauncherInterfaceRequest()
	if err != nil {
		return fmt.Errorf("could not make launcher request object: %s", err)
	}
	context.ConnectToEnvService(req)
	defer func() {
		c := req.ToChannel()
		(&c).Close()
	}()
	contReq, _, err := sys.NewComponentControllerInterfaceRequest()
	if err != nil {
		return fmt.Errorf("error creating component controller request: %s", err)
	}

	err = pxy.CreateComponent(*info, contReq)
	if err != nil {
		return fmt.Errorf("error starting system updater: %s", err)
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
	updateVersion, err := latestPackageVersion("update")
	if err != nil {
		return "", err
	}

	path := filepath.Join(packageDir("update", updateVersion), "packages")

	m, err := readManifest(path)
	if err != nil {
		return "", err
	}

	systemImageVersion, err := latestPackageVersion("system_image")
	if err != nil {
		return "", err
	}

	if merkle, ok := m[fmt.Sprintf("system_image/%s", systemImageVersion)]; ok {
		return merkle, nil
	} else {
		return "", fmt.Errorf("system_image package not in %q", path)
	}
}

func connectToUpdateSrvc() (*amber.ControlInterface, error) {
	context := context.CreateFromStartupInfo()
	req, pxy, err := amber.NewControlInterfaceRequest()

	if err != nil {
		return nil, fmt.Errorf("error getting control interface: %s", err)
	}

	context.ConnectToEnvService(req)
	return pxy, nil
}

func fetchPackage(p *pkg.Package, amber *amber.ControlInterface) error {
	h, err := amber.GetUpdateComplete(p.Name, nil, &p.Merkle)
	if err != nil {
		return fmt.Errorf("fetch: failed submitting update request: %s", err)
	}
	defer h.Close()

	signals, err := zxwait.Wait(*h.Handle(), zx.SignalChannelPeerClosed|zx.SignalChannelReadable,
		zx.TimensecInfinite)
	if err != nil {
		return fmt.Errorf("fetch: error waiting on result channel: %s", err)
	}

	buf := make([]byte, 128)
	if signals&zx.SignalChannelReadable == zx.SignalChannelReadable {
		_, _, err := h.Read(buf, []zx.Handle{}, 0)
		if err != nil {
			return fmt.Errorf("fetch: error reading channel %s", err)
		}
	} else {
		return fmt.Errorf("fetch: reply channel was not readable")
	}
	return nil
}

func statMerkle(path string) ([]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return []byte{}, err
	}
	defer f.Close()

	m := &merkle.Tree{}

	if _, err = m.ReadFrom(f); err == nil {
		return m.Root(), nil
	}

	return []byte{}, err
}

func latestPackageVersion(name string) (string, error) {
	path := filepath.Join(packagesDir, name)
	dir, err := os.Open(path)
	if err != nil {
		if os.IsNotExist(err) {
			return "", NewErrNoPackage(name)
		}
		return "", err
	}
	defer dir.Close()

	ents, err := dir.Readdirnames(0)
	if len(ents) == 0 {
		return "", NewErrNoPackage(name)
	}

	_, version, err := GreatestIntStr(ents)
	if err != nil {
		if err == ErrNoInput {
			return "", fmt.Errorf("package has no versions")
		}
		return "", err
	}

	return version, nil
}

func packageDir(name, version string) string {
	return filepath.Join(packagesDir, name, version)
}

func latestPackageDir(name string) (string, error) {
	version, err := latestPackageVersion(name)
	if err != nil {
		return "", err
	}

	return packageDir(name, version), nil
}

func updatePackageContentMerkle() (string, error) {
	path, err := latestPackageDir("update")
	if err != nil {
		return "", err
	}

	merkleBytes, err := statMerkle(filepath.Join(path, "meta", "contents"))
	if err != nil {
		log.Printf("sys_upd_mon: merkle computation of \"update\" package's \"meta/contents\" file failed. " +
			"treating \"update\" package as nonexistent")
		return "", NewErrNoPackage("update")
	}

	merkle := hex.EncodeToString(merkleBytes)
	log.Printf("sys_upd_mon: \"update\" package's \"meta/contents\" merkle: %q", merkle)

	return merkle, nil
}

var ErrNoInput = fmt.Errorf("no inputs supplied")

type ErrNan string

func (e ErrNan) Error() string {
	return string(e)
}

func GreatestIntStr(s []string) (int, string, error) {
	i := -1
	var str string

	if len(s) == 0 {
		return i, str, ErrNoInput
	}

	for _, s := range s {
		cand, err := strconv.ParseInt(s, 10, 0)
		if err != nil {
			return -1, "", ErrNan(
				fmt.Sprintf("string %q could not be parsed as a number, error: %s", s, err))
		}

		if int(cand) > i {
			i = int(cand)
			str = s
		}
	}

	return i, str, nil
}
