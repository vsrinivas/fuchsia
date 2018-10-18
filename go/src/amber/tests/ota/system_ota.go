// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_ota

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	tuf_data "github.com/flynn/go-tuf/data"
)

var (
	fuchsiaBuildDir = flag.String("fuchsia-build-dir", os.Getenv("FUCHSIA_BUILD_DIR"), "fuchsia build dir")
	sshConfig       = flag.String("ssh-config", "", "ssh config file")
	repoDir         = flag.String("repo-dir", "", "amber repository dir")
	zirconToolsDir  = flag.String("zircon-tools-dir", os.Getenv("ZIRCON_TOOLS_DIR"), "zircon tools dir")
	localHostname   = flag.String("local-hostname", "", "local hostname")
	deviceName      = flag.String("device", "", "device name")
	deviceHostname  = flag.String("device-hostname", "", "device hostname")

	localDevmgr []byte
	noSuchFile  = []byte(": No such file or directory\n")
)

const (
	rebootFile       = "/tmp/ota-test-waiting-for-reboot"
	remoteDevmgrPath = "/boot/config/devmgr"
)

func needFuchsiaBuildDir() {
	if *fuchsiaBuildDir == "" {
		log.Fatalf("either pass -fuchsia-build-dir or set $FUCHSIA_BUILD_DIR")
	}
}

func needZirconToolsDir() {
	if *zirconToolsDir == "" {
		log.Fatalf("either pass -zircon-tools-dir or set $ZIRCON_TOOLS_DIR")
	}
}

func init() {
	flag.Parse()

	if *deviceName != "" && *deviceHostname != "" {
		log.Fatalf("-device and -device-hostname are incompatible")
	}

	if *sshConfig == "" && *fuchsiaBuildDir != "" {
		*sshConfig = filepath.Join(*fuchsiaBuildDir, "ssh-keys", "ssh_config")
	}

	if *repoDir == "" {
		needFuchsiaBuildDir()
		*repoDir = filepath.Join(*fuchsiaBuildDir, "amber-files", "repository")
	}

	var err error
	if *localHostname == "" {
		needZirconToolsDir()
		*localHostname, err = netaddr("--local", *deviceName)
		if err != nil {
			log.Fatalf("ERROR: netaddr failed: %s", err)
		}
		if *localHostname == "" {
			log.Fatalf("unable to determine the local hostname")
		}
	}

	if *deviceHostname == "" {
		needZirconToolsDir()
		*deviceHostname, err = netaddr("--nowait", "--timeout=1000", "--fuchsia", *deviceName)
		if err != nil {
			log.Fatalf("ERROR: netaddr failed: %s", err)
		}
		if *deviceHostname == "" {
			log.Fatalf("unable to determine the device hostname")
		}
	}

	needFuchsiaBuildDir()

	// We want to make sure that /boot/config/devmgr config file changed to the
	// value we expect. First, read the file from the build.
	localDevmgrPath := filepath.Join(*fuchsiaBuildDir, "obj", "build", "images", "devmgr_config.txt")
	localDevmgr, err = ioutil.ReadFile(localDevmgrPath)
	if err != nil {
		log.Fatalf("failed to read %q: %s", localDevmgrPath, err)
	}

	// Serve the repository before the test begins.
	serveRepository(*repoDir)

	// Tell the device to connect to our repository.
	registerAmberSource()
}

// Prepare the device for an OTA.
func PrepareOTA(t *testing.T) {
	// Make sure we can ping the device.
	waitForDeviceToPing(t)

	// Make sure that the device does not have the /boot/config/devmgr we
	// are OTA-ing to. In addition, make sure our package and system
	// package do not exist on the system.
	remoteDevmgr := ReadRemotePath(t, remoteDevmgrPath)
	if bytes.Equal(localDevmgr, remoteDevmgr) {
		t.Fatalf("%q should not be:\n\n%s", remoteDevmgrPath, remoteDevmgr)
	}
}

// Trigger an OTA and verify the device successfully came up.
func TriggerOTA(t *testing.T) {
	// We need a way to tell if a reboot happened. We'll do this by writing a dummy file to /tmp, which will
	// be wiped on reboot.
	touchRebootFile(t)

	log.Printf("triggering OTA")

	stdout, stderr, err := ssh("amber_ctl", "system_update")
	log.Printf("%s\n%s", stdout, stderr)
	if err != nil {
		t.Fatalf("failed to trigger OTA: %s", err)
	}

	// Wait until our reboot file is removed, and for a few file systems to mount.
	waitForDeviceToReboot(t)
	waitForDevicePath(t, "/boot")
	waitForDevicePath(t, "/system")
	waitForDevicePath(t, "/pkgfs")

	// Verify that we are now running the expected /boot/config/devmgr.
	remoteDevmgr := ReadRemotePath(t, remoteDevmgrPath)
	if !bytes.Equal(localDevmgr, remoteDevmgr) {
		t.Fatalf("expected %q to be:\n\n%s\n\nbut instead got:\n\n%s", remoteDevmgrPath, localDevmgr, remoteDevmgr)
	}
}

// Read a file off the remote device.
func ReadRemotePath(t *testing.T, path string) []byte {
	stdout, stderr, err := ssh(fmt.Sprintf("while read line; do echo \"$line\"; done < %s", path))
	if err != nil {
		t.Fatalf("failed to read %q: %s: %s", path, err, string(stderr))
	}

	return stdout
}

// Check if a file exists on the remote device.
func RemoteFileExists(t *testing.T, path string) bool {
	_, stderr, err := ssh("ls", path)
	if err == nil {
		return true
	}

	if !bytes.HasSuffix(stderr, noSuchFile) {
		t.Fatalf("error reading %q: %s", path, stderr)
	}

	return false
}

type loggingWriter struct {
	http.ResponseWriter
	status int
}

func (lw *loggingWriter) WriteHeader(status int) {
	lw.status = status
	lw.ResponseWriter.WriteHeader(status)
}

func serveRepository(repoDir string) {
	go func() {
		http.Handle("/", http.FileServer(http.Dir(repoDir)))
		err := http.ListenAndServe(":8083", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			lw := &loggingWriter{w, 0}
			http.DefaultServeMux.ServeHTTP(lw, r)
			log.Printf("%s [pm serve] %d %s\n",
				time.Now().Format("2006-01-02 15:04:05"), lw.status, r.RequestURI)
		}))
		if err != nil {
			log.Fatal(err)
		}
	}()
	time.Sleep(1 * time.Second)
}

func runOutput(name string, arg ...string) ([]byte, []byte, error) {
	log.Printf("running: %s %q", name, arg)
	c := exec.Command(name, arg...)
	var o bytes.Buffer
	var e bytes.Buffer
	c.Stdout = &o
	c.Stderr = &e
	err := c.Run()
	stdout := o.Bytes()
	stderr := e.Bytes()
	log.Printf("stdout: %s", stdout)
	log.Printf("stderr: %s", stderr)
	return stdout, stderr, err
}

func ssh(arg ...string) ([]byte, []byte, error) {
	var a []string
	if *sshConfig == "" {
		a = make([]string, 0, len(arg)+1)
		a = append(append(a, *deviceHostname), arg...)
	} else {
		a = make([]string, 0, len(arg)+3)
		a = append(append(a, "-F", *sshConfig, *deviceHostname), arg...)
	}

	return runOutput("/usr/bin/ssh", a...)
}

func netaddr(arg ...string) (string, error) {
	stdout, stderr, err := runOutput(filepath.Join(*zirconToolsDir, "netaddr"), arg...)
	if err != nil {
		if len(stderr) != 0 {
			return "", fmt.Errorf("netaddr failed: %s: %s", err, string(stderr))
		} else {
			return "", fmt.Errorf("netaddr failed: %s", err)
		}
	}
	return strings.TrimRight(string(stdout), "\n"), nil
}

func waitForDeviceToPing(t *testing.T) {
	log.Printf("waiting for device %q to ping", *deviceHostname)
	path, err := exec.LookPath("/bin/ping")
	if err != nil {
		t.Fatal(err)
	}

	for {
		out, err := exec.Command(path, "-c", "1", "-W", "1", *deviceHostname).Output()
		if err == nil {
			break
		}
		log.Printf("%s - %s", err, out)

		time.Sleep(1 * time.Second)
	}
	log.Printf("device up")
}

func touchRebootFile(t *testing.T) {
	log.Printf("touching %q", rebootFile)
	_, _, err := ssh(fmt.Sprintf("echo > %s", rebootFile))
	if err != nil {
		t.Fatal(err)
	}
}

type keyConfig struct {
	Type  string
	Value string
}

type statusConfig struct {
	Enabled bool
}

type sourceConfig struct {
	Id           string       `json:"id"`
	RepoUrl      string       `json:"repoUrl"`
	BlobRepoUrl  string       `json:"blobRepoUrl"`
	RootKeys     []keyConfig  `json:"rootKeys"`
	StatusConfig statusConfig `json:"statusConfig"`
}

func registerAmberSource() {
	log.Printf("registering devhost as update source")

	f, err := os.Open(filepath.Join(*repoDir, "root.json"))
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()

	var signed tuf_data.Signed
	if err := json.NewDecoder(f).Decode(&signed); err != nil {
		log.Fatal(err)
	}

	var root tuf_data.Root
	if err := json.Unmarshal(signed.Signed, &root); err != nil {
		log.Fatal(err)
	}

	var rootKeys []keyConfig
	for _, keyId := range root.Roles["root"].KeyIDs {
		key := root.Keys[keyId]

		rootKeys = append(rootKeys, keyConfig{
			Type:  key.Type,
			Value: key.Value.Public.String(),
		})
	}

	hostname := strings.SplitN(*localHostname, "%", 2)[0]
	repoUrl := fmt.Sprintf("http://[%s]:8083", hostname)
	configUrl := fmt.Sprintf("%s/devhost/config.json", repoUrl)

	config, err := json.Marshal(&sourceConfig{
		Id:          "devhost",
		RepoUrl:     repoUrl,
		BlobRepoUrl: fmt.Sprintf("%s/blobs", repoUrl),
		RootKeys:    rootKeys,
		StatusConfig: statusConfig{
			Enabled: true,
		},
	})
	if err != nil {
		log.Fatal(err)
	}
	configHash := sha256.Sum256(config)
	configHashString := hex.EncodeToString(configHash[:])

	configDir := filepath.Join(*repoDir, "devhost")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		log.Fatal(err)
	}

	configPath := filepath.Join(configDir, "config.json")
	log.Printf("writing %q", configPath)
	if err := ioutil.WriteFile(configPath, config, 0644); err != nil {
		log.Fatal(err)
	}

	stdout, stderr, err := ssh("amber_ctl", "add_src", "-f", configUrl, "-h", configHashString)
	log.Printf("%s\n%s", stdout, stderr)
	if err != nil {
		log.Fatal(err)
	}
}

func waitForDeviceToReboot(t *testing.T) {
	for {
		log.Printf("waiting for Device to reboot")
		if !RemoteFileExists(t, rebootFile) {
			break
		}

		time.Sleep(1 * time.Second)
	}
}

func waitForDevicePath(t *testing.T, path string) {
	for {
		log.Printf("waiting for %s to mount", path)
		_, _, err := ssh("ls", path)
		if err == nil {
			break
		}

		time.Sleep(1 * time.Second)
	}
}
