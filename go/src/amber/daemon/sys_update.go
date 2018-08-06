package daemon

import (
	"amber/pkg"
	"bytes"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"sync/atomic"
	"time"

	"app/context"
	"fidl/fuchsia/amber"
	"fidl/fuchsia/sys"
	"syscall/zx"
	"syscall/zx/zxwait"

	"fuchsia.googlesource.com/merkle"
)

var updaterDir = filepath.Join("/pkgfs", "packages", "update")
var updaterBin = filepath.Join("bin", "app")

type SystemUpdateMonitor struct {
	halt     uint32
	daemon   *Daemon
	checkNow chan struct{}
	// if auto is allowed to be reset after instantiation, changes must be made
	// in the run loop to avoid a panic
	auto bool
}

type ErrNoUpdater string

func (e ErrNoUpdater) Error() string {
	return "no system_updater is available on the system"
}

func NewErrNoUpdater() ErrNoUpdater {
	return ""
}

func NewSystemUpdateMonitor(d *Daemon, a bool) *SystemUpdateMonitor {
	return &SystemUpdateMonitor{daemon: d, halt: 0, checkNow: make(chan struct{}), auto: a}
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
	path, err := latestSystemUpdater()
	var contentsPath string
	merkle := []byte{}
	if err != nil {
		if _, ok := err.(ErrNoUpdater); !ok {
			log.Printf("sys_upd_mon: unexpected error reading updater: %s", err)
			return
		}
	} else {
		contentsPath = filepath.Join(path, "meta", "contents")
		m, err := statMerkle(contentsPath)
		if err != nil {
			log.Printf("sys_upd_mon: merkle computation of contents file failed " +
				"treating 'update' package as nonexistent")
		} else {
			merkle = m
		}
	}

	amber, err := connectToUpdateSrvc()
	if err != nil {
		log.Printf("sys_upd_mon: binding to update service failed: %s", err)
		return
	}

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

		updatePkg := &pkg.Package{Name: fmt.Sprintf("/update/%d", 0)}
		if err = fetchPackage(updatePkg, amber); err != nil {
			log.Printf("sys_upd_mon: unable to fetch package update: %s", err)
			continue
		}

		// if not system updater was available at all before, check for the latest one
		if path == "" {
			path, err = latestSystemUpdater()
			if err != nil {
				continue
			}
			contentsPath = filepath.Join(path, "meta", "contents")
		}
		newMerkle, err := statMerkle(contentsPath)
		if err != nil {
			log.Printf("sys_upd_mon: stat of contents file failed: %s", err)
			continue
		}

		if !bytes.Equal(newMerkle, merkle) {
			log.Println("System update starting...")
			launchDesc := sys.LaunchInfo{Url: "system_updater"}
			if err = runProgram(&launchDesc); err != nil {
				log.Printf("sys_upd_mon: updater failed to start: %s", err)
			}
		} else {
			log.Println("sys_upd_mon: no newer system version available")
		}
		merkle = newMerkle
	}
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

func latestSystemUpdater() (string, error) {
	dir, err := os.Open(updaterDir)
	if err != nil {
		if os.IsNotExist(err) {
			return "", NewErrNoUpdater()
		}
		return "", err
	}

	defer dir.Close()

	ents, err := dir.Readdirnames(0)
	if len(ents) == 0 {
		return "", NewErrNoUpdater()
	}

	_, verDir, err := GreatestIntStr(ents)

	if err != nil {
		if err == ErrNoInput {
			return "", fmt.Errorf("package has no versions")
		}
		return "", err
	}

	return filepath.Join(updaterDir, verDir), nil
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
