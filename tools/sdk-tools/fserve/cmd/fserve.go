// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"archive/tar"
	"compress/gzip"
	"context"
	"crypto/md5"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

var (
	// ExecCommand exports exec.Command as a variable so it can be mocked.
	ExecCommand = exec.Command
	// Logger level.
	level = logger.InfoLevel
)

// Allow mocking of Kill.
type osProcess interface {
	Kill() error
}

func defaultFindProcess(pid int) (osProcess, error) {
	return os.FindProcess(pid)
}

var findProcess = defaultFindProcess

// allow mocking of syscalls.Wait4

func defaultsyscallWait4(pid int, wstatus *syscall.WaitStatus, flags int, usage *syscall.Rusage) (int, error) {
	return syscall.Wait4(pid, wstatus, flags, usage)
}

var syscallWait4 = defaultsyscallWait4

const logFlags = log.Ltime

type sdkProvider interface {
	GetSDKDataPath() string
	GetToolsDir() (string, error)
	GetAvailableImages(version string, bucket string) ([]sdkcommon.GCSImage, error)
	GetAddressByName(deviceName string) (string, error)
	RunSSHCommand(targetAddress string, sshConfig string, privateKey string, verbose bool, sshArgs []string) (string, error)
}

func main() {
	var (
		err error
	)

	sdk, err := sdkcommon.New()
	if err != nil {
		log.Fatalf("Could not initialize SDK: %v", err)
	}

	helpFlag := flag.Bool("help", false, "Show the usage message")
	repoFlag := flag.String("repo-dir", "", "Specify the path to the package repository.")
	bucketFlag := flag.String("bucket", "", "Specify the GCS bucket for the prebuilt packages.")
	imageFlag := flag.String("image", "", "Specify the GCS file name for prebuild packages.")
	nameFlag := flag.String("name", "devhost", "Name is used as the update channel identifier, as reported by fuchsia.update.channel.Provider.")
	repoPortFlag := flag.String("server-port", "", "Port number to use when serving the packages.")
	killFlag := flag.Bool("kill", false, "Kills any existing package manager server.")
	prepareFlag := flag.Bool("prepare", false, "Downloads any dependencies but does not start the package server.")
	versionFlag := flag.String("version", sdk.GetSDKVersion(), "SDK Version to use for prebuilt packages.")
	flag.Var(&level, "level", "Output verbosity, can be fatal, error, warning, info, debug or trace.")

	// target related options
	privateKeyFlag := flag.String("private-key", "", "Uses additional private key when using ssh to access the device.")
	deviceNameFlag := flag.String("device-name", "", `Serves packages to a device with the given device hostname. Cannot be used with --device-ip."
	  If neither --device-name nor --device-ip are specified, the device-name configured using fconfig.sh is used.`)
	deviceIPFlag := flag.String("device-ip", "", `Serves packages to a device with the given device ip address. Cannot be used with --device-name."
	  If neither --device-name nor --device-ip are specified, the device-name configured using fconfig.sh is used.`)
	sshConfigFlag := flag.String("sshconfig", "", "Use the specified sshconfig file instead of fssh's version.")

	flag.Parse()

	log := logger.NewLogger(level, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "fserve ")
	log.SetFlags(logFlags)
	ctx := logger.WithLogger(context.Background(), log)

	if *helpFlag {
		usage()
		os.Exit(0)
	}

	// Set the defaults from the SDK if not present.
	if *repoFlag == "" {
		repoDir, err := sdk.GetDefaultPackageRepoDir()
		if err != nil {
			log.Fatalf("Could not determine default package directory: %v\n", err)
		}
		flag.Set("repo-dir", repoDir)
	}

	if *bucketFlag == "" {
		bucket, err := sdk.GetDefaultGCSBucket()
		if err != nil {
			log.Fatalf("Could not determine default GCS bucket: %v\n", err)
		}
		flag.Set("bucket", bucket)
	}

	if *imageFlag == "" {
		image, err := sdk.GetDefaultGCSImage()
		if err != nil {
			log.Fatalf("Could not determine default GCS image: %v\n", err)
		}
		flag.Set("image", image)
	}

	if *repoPortFlag == "" {
		image, err := sdk.GetDefaultPackageServerPort()
		if err != nil {
			log.Fatalf("Could not determine default package server port: %v\n", err)
		}
		flag.Set("server-port", image)
	}
	if *versionFlag == "" {
		flag.Set("version", sdk.GetSDKVersion())
	}

	targetAddress, err := sdk.ResolveTargetAddress(*deviceIPFlag, *deviceNameFlag)
	if err != nil {
		log.Fatalf("Could not determine target address with specified device-ip: %v, device-name: %v: %v", *deviceIPFlag, *deviceNameFlag, err)
	}
	log.Debugf("Using target address: %v", targetAddress)

	if *versionFlag == "" {
		log.Fatalf("SDK version not known. Use --version to specify it manually.\n")
	}

	if *imageFlag == "" {
		log.Errorf("Image not specified. Use --image to specify it manually.\n")
		// Don't exit here, prepare will print a list of valid image names for the user.
	}

	// Kill any server on the same port
	if err = killServers(ctx, *repoPortFlag); err != nil {
		log.Fatalf("Could not kill existing package servers: %v\n", err)
	}
	if *killFlag {
		os.Exit(0)
	}

	if err = prepare(ctx, sdk, *versionFlag, *repoFlag, *bucketFlag, *imageFlag); err != nil {
		log.Fatalf("Could not prepare packages: %v\n", err)
	}
	if *prepareFlag {
		os.Exit(0)
	}

	_, err = startServer(sdk, *repoFlag, *repoPortFlag)
	if err != nil {
		log.Fatalf("Could start package server: %v\n", err)
	}

	// connect the device to the package server
	if err := setPackageSource(ctx, sdk, *repoPortFlag, *nameFlag, targetAddress, *sshConfigFlag, *privateKeyFlag); err != nil {
		log.Fatalf("Could set package server source on device: %v\n", err)
	}

	os.Exit(0)
}

func usage() {
	fmt.Printf("Usage: %s [options]\n", filepath.Base(os.Args[0]))
	flag.PrintDefaults()
}

// prepare dowloads and untars the packages from GCS if needed.
func prepare(ctx context.Context, sdk sdkcommon.SDKProperties, version string, repoPath string, bucket string, image string) error {
	log := logger.LoggerFromContext(ctx)
	if image == "list" || image == "" {
		printValidImages(sdk, version, bucket)
		os.Exit(1)
	}

	targetPackage := sdk.GetPackageSourcePath(version, bucket, image)
	imageFilename := fmt.Sprintf("%v_%v.tar.gz", version, image)

	log.Infof("Serving %v packages for SDK version %v from %v\n", image, version, targetPackage)

	err := downloadImageIfNeeded(ctx, sdk, version, bucket, targetPackage, imageFilename, repoPath)
	if err != nil {
		return fmt.Errorf("Could not download %v:  %v", targetPackage, err)
	}
	return nil
}

func killServers(ctx context.Context, portNum string) error {
	log := logger.LoggerFromContext(ctx)
	// First get all the pm commands for the user
	cmd := ExecCommand("pgrep", "pm")
	output, err := cmd.Output()
	if err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			if string(exitError.Stderr) != "" {
				err = errors.New(string(exitError.Stderr))
			}
			// Special case for mac compared to Linux. If there are no
			// processes found on Linux, the exit code is 0, but on mac
			// it is 1 with no text on either stdout or stderr.
			// In this case we want to treat it as a non-error. However,
			// if there is an error message or if the exit code is not 1,
			// return an error.
			if runtime.GOOS == "darwin" && exitError.ExitCode() == 1 && len(exitError.Stderr) == 0 {
				return nil
			}
			return fmt.Errorf("Error running pgrep: %v", err)
		}
		return err
	}

	if len(output) == 0 {
		return nil
	}

	pids := []string{}
	for _, p := range strings.Split(string(output), "\n") {
		tp := strings.TrimSpace(p)
		if tp != "" {
			pids = append(pids, tp)
		}
	}

	// Now run ps to look for pm
	cmd = ExecCommand("ps", pids...)
	output, err = cmd.Output()
	if err != nil {
		exiterr := err.(*exec.ExitError)
		err = errors.New(string(exiterr.Stderr))
		return fmt.Errorf("Error running ps: %v", err)
	}

	// Split on lines
	for _, line := range strings.Split(string(output), "\n") {
		line = strings.TrimSpace(line)
		parts := strings.Fields(line)
		if len(parts) == 0 {
			continue
		}
		// skip header
		if parts[0] == "PID" {
			continue
		}

		// check the process name ends in /pm
		if strings.HasSuffix(parts[4], "/pm") {
			// if a port number was passed in, match that so we avoid killing unrelated servers.
			portMatches := true
			if portNum != "" {
				portMatches = false
				for _, arg := range parts {
					if arg == fmt.Sprintf(":%s", portNum) {
						log.Debugf("matched port %v\n", parts[4:])
						portMatches = true
					}
				}
			}
			if portMatches {
				pid, _ := strconv.Atoi(parts[0])
				proc, _ := findProcess(pid)
				return proc.Kill()
			}

		}
	}

	logger.Infof(context.Background(), "No running package servers found.\n")
	return nil
}

// startServer starts the `pm serve` command and returns the command object.
// The server is started in the background.
func startServer(sdk sdkProvider, repoPath string, repoPort string) (*exec.Cmd, error) {
	toolsDir, err := sdk.GetToolsDir()
	if err != nil {
		return nil, fmt.Errorf("Could not determine tools directory %v", err)
	}
	cmd := filepath.Join(toolsDir, "pm")

	args := []string{"serve", "-repo", repoPath, "-l", fmt.Sprintf(":%s", repoPort)}

	proc := ExecCommand(cmd, args...)
	proc.Stdout = os.Stdout
	proc.Stderr = os.Stderr
	err = proc.Start()
	if err != nil {
		return proc, err
	}

	// Once the process is started, wait for 2 seconds to look for an early exit
	// this allows catching things like "port in use" or other errors from pm before
	// losing control of pm.
	pid := proc.Process.Pid
	var wstat syscall.WaitStatus
	for i := 0; i < 4; i++ {
		_, err := syscallWait4(pid, &wstat, syscall.WNOHANG, nil)
		if err != nil {
			return proc, fmt.Errorf("Error waiting on pm: %v", err)
		}
		if wstat.Exited() && wstat.ExitStatus() != 0 {
			return proc, fmt.Errorf("Server started then exited with code %v", wstat.ExitStatus())
		}
		time.Sleep(500 * time.Millisecond)
	}
	return proc, nil
}

// printValidImages prints the bucket and image names found on GCS.
func printValidImages(sdk sdkProvider, version string, bucket string) error {
	images, err := sdk.GetAvailableImages(version, bucket)
	if err != nil {
		return fmt.Errorf("Could not get list of images: %v", err)
	}
	fmt.Printf("Available images are:\n")
	for _, image := range images {
		fmt.Printf("%s: %s\n", image.Bucket, image.Name)
	}
	return nil
}

// downloadImageIfNeeded downloads from GCS the packages for the given prebuillt image.
// The md5 hash of the tarball is stored to check for differences from the downloaded version.
// if the hash matches, the tarball is not un-tarred.
func downloadImageIfNeeded(ctx context.Context, sdk sdkProvider, version string, bucket string, srcPath string, imageFilename string, repoPath string) error {
	// Validate the image is found
	localImagePath := filepath.Join(sdk.GetSDKDataPath(), imageFilename)
	packageDir := filepath.Dir(repoPath)
	log := logger.LoggerFromContext(ctx)

	if !sdkcommon.FileExists(localImagePath) {
		_, err := sdkcommon.GCSFileExists(srcPath)
		if err != nil {
			if strings.Contains(err.Error(), "One or more URLs matched no objects") {
				log.Errorf("Could not locate %v: %v\n", srcPath, err)
				printValidImages(sdk, version, bucket)
			}
			return err
		}
		_, err = sdkcommon.GCSCopy(srcPath, localImagePath)
		if err != nil {
			return fmt.Errorf("Could not copy image from %v to %v: %v", srcPath, localImagePath, err)
		}
	} else {
		log.Infof("Skipping download, packages tarball exists\n")
	}

	// The checksum file contains the output from `md5`. This is used to detect content changes in the packages file.
	checksumFile := filepath.Join(packageDir, "packages.md5")
	removePackageDir := false
	md5Hash, err := calculateMD5(localImagePath)
	if err != nil {
		return fmt.Errorf("could not checksum file %v: %v", localImagePath, err)
	}
	if sdkcommon.FileExists(checksumFile) {
		content, err := ioutil.ReadFile(checksumFile)
		if err != nil {
			log.Warningf("Could not read checksum file %v: %v\n", checksumFile, err)
			removePackageDir = true
		}
		parts := strings.Fields(string(content))
		removePackageDir = removePackageDir || md5Hash != parts[0]

		// Look for old file name
		if len(parts) > 1 {
			oldFile := strings.TrimSpace(parts[1])
			if sdkcommon.FileExists(oldFile) && oldFile != localImagePath {
				err = os.Remove(oldFile)
				if err != nil {
					logger.Errorf(ctx, "Could not remove old archive %v: %v\n", oldFile, err)
				}
			} else {
				logger.Debugf(ctx, "%s does not exist or is the same as %s\n", oldFile, localImagePath)
			}
		} else {
			logger.Debugf(ctx, "Could not parse old file name from %v", parts)
		}
	} else {
		logger.Debugf(ctx, "%s does not exist so can't check the checksum\n", checksumFile)
		removePackageDir = true
	}

	if removePackageDir {
		logger.Debugf(context.Background(), "Removing directory %s\n", packageDir)
		err := os.RemoveAll(packageDir)
		if err != nil {
			logger.Fatalf(ctx, "Could not remove directory %v: %v", packageDir, err)
		}

		if err := os.MkdirAll(packageDir, 0755); err != nil {
			logger.Fatalf(ctx, "Could not create directory %v: %v", packageDir, err)
		}
	}

	if !sdkcommon.DirectoryExists(filepath.Join(packageDir, "amber-files")) {
		if err = extractTar(ctx, localImagePath, packageDir); err != nil {
			logger.Fatalf(ctx, "Could not create extract %v into %v: %v", localImagePath, packageDir, err)
		}
	}
	md5File, err := os.Create(checksumFile)
	if err != nil {
		logger.Fatalf(ctx, "Could not write checksum file %v: %v\n", checksumFile, err)
	}
	defer md5File.Close()
	md5File.WriteString(fmt.Sprintf("%v %v\n", md5Hash, localImagePath))

	return nil
}

// setPackageSource sets the URL for the package server on the target device.
func setPackageSource(ctx context.Context, sdk sdkProvider, repoPort string, name string,
	targetAddress string, sshConfig string, privateKey string) error {

	var (
		err error
		log = logger.LoggerFromContext(ctx)
	)

	if targetAddress == "" {
		return errors.New("target address required")
	}

	log.Debugf("Using target address %v", targetAddress)

	hostIP, err := getHostIPAddressFromTarget(sdk, targetAddress, sshConfig, privateKey)
	if err != nil {
		return fmt.Errorf("Could not get host address from target %v: %v", targetAddress, err)
	}
	// A simple heuristic for "is an ipv6 address", URL encase escape
	// the address.
	if strings.Contains(hostIP, ":") {
		hostIP = strings.ReplaceAll(hostIP, "%", "%25")
		hostIP = fmt.Sprintf("[%s]", hostIP)
	}

	sshArgs := []string{"amber_ctl", "add_src", "-n", name, "-f",
		fmt.Sprintf("http://%v:%v/config.json", hostIP, repoPort)}

	verbose := level == logger.DebugLevel || level == logger.TraceLevel
	if _, err = sdk.RunSSHCommand(targetAddress, sshConfig, privateKey, verbose, sshArgs); err != nil {
		return fmt.Errorf("Could not set package server address on device: %v", err)
	}

	return nil
}

// getHostIPAddressFromTarget returns the host address reported from the SSH connection on the target device.
func getHostIPAddressFromTarget(sdk sdkProvider, targetAddress string, sshConfig string, privateKey string) (string, error) {

	var sshArgs = []string{"echo", "$SSH_CONNECTION"}

	verbose := level == logger.DebugLevel || level == logger.TraceLevel
	connectionString, err := sdk.RunSSHCommand(targetAddress, sshConfig, privateKey, verbose, sshArgs)

	if err != nil {
		return "", err
	}

	parts := strings.Fields(connectionString)

	return strings.TrimSpace(parts[0]), nil
}

// extractTar extracts the contents from the srcFile tarball into the destDir.
func extractTar(ctx context.Context, srcFile string, destDir string) error {

	log := logger.LoggerFromContext(ctx)
	f, err := os.Open(srcFile)
	if err != nil {
		return fmt.Errorf("Could not open %v: %v", srcFile, err)
	}
	defer f.Close()
	uncompressedStream, err := gzip.NewReader(f)
	if err != nil {
		return fmt.Errorf("extractTar: NewReader failed: %v", err)
	}

	tarReader := tar.NewReader(uncompressedStream)

	for true {
		header, err := tarReader.Next()

		if err == io.EOF {
			break
		}

		if err != nil {
			return fmt.Errorf("extractTar: Next() failed: %v", err)
		}

		switch header.Typeflag {
		case tar.TypeDir:
			dir := filepath.Join(destDir, header.Name)
			log.Debugf("mkdir %v", dir)
			if err := os.MkdirAll(dir, 0755); err != nil {
				return fmt.Errorf("extractTar: Mkdir() failed: %v", err)
			}
		case tar.TypeReg:
			newFile := filepath.Join(destDir, header.Name)
			log.Debugf("extracting %v", newFile)
			if err := os.MkdirAll(filepath.Dir(newFile), 0755); err != nil {
				return fmt.Errorf("extractTar: MkdirAll in Create() failed: %v", err)
			}
			outFile, err := os.Create(newFile)
			if err != nil {
				return fmt.Errorf("extractTar: Create() failed: %v", err)
			}
			if _, err := io.Copy(outFile, tarReader); err != nil {
				return fmt.Errorf("extractTar: Copy() failed: %v", err)
			}
			outFile.Close()

		default:
			return fmt.Errorf("extractTar: uknown type: %v in %v", header.Typeflag, header.Name)
		}
	}
	return nil
}

// calculateMD5 for the given file.
func calculateMD5(filename string) (string, error) {
	f, err := os.Open(filename)
	if err != nil {
		return "", fmt.Errorf("Cannot open %v: %v", filename, err)
	}
	defer f.Close()

	h := md5.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", fmt.Errorf("Cannot calculate md5 for %v: %v", filename, err)
	}
	return fmt.Sprintf("%x", h.Sum(nil)), nil
}
