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
	// OsStat exports os.Stat as a variable so it can be mocked.
	OsStat = os.Stat
	// Logger level.
	level = logger.InfoLevel
)

var osExit = os.Exit

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
	RunFFX(args []string, interactive bool) (string, error)
	RunSSHCommand(targetAddress string, sshConfig string, privateKey string, sshPort string,
		verbose bool, sshArgs []string) (string, error)
}

func main() {
	var (
		err error
	)

	dataPathFlag := flag.String("data-path", "", "Specifies the data path for SDK tools. Defaults to $HOME/.fuchsia.")
	helpFlag := flag.Bool("help", false, "Show the usage message")
	repoFlag := flag.String("repo-dir", "", "Specify the path to the package repository.")
	bucketFlag := flag.String("bucket", "", "Specify the GCS bucket for the prebuilt packages.")
	imageFlag := flag.String("image", "", "Specify the GCS file name for prebuild packages.")
	nameFlag := flag.String("name", "devhost", "Name is used as the update channel identifier, as reported by fuchsia.update.channel.Provider.")
	repoPortFlag := flag.String("server-port", "", "Port number to use when serving the packages.")
	killFlag := flag.Bool("kill", false, "Kills any existing package manager server.")
	cleanFlag := flag.Bool("clean", false, "Cleans the package repository first.")
	prepareFlag := flag.Bool("prepare", false, "Downloads any dependencies but does not start the package server.")
	versionFlag := flag.String("version", "", "SDK Version to use for prebuilt packages.")
	persistFlag := flag.Bool("persist", false, "Persist repository metadata to allow serving resolved packages across reboot.")
	packageArchiveFlag := flag.String("package-archive", "",
		"Specify the source package archive in .tgz or directory format. If specified, no packages are downloaded from GCS.")
	flag.Var(&level, "level", "Output verbosity, can be fatal, error, warning, info, debug or trace.")

	// target related options
	privateKeyFlag := flag.String("private-key", "", "Uses additional private key when using ssh to access the device.")
	deviceNameFlag := flag.String("device-name", "", `Serves packages to a device with the given device hostname. Cannot be used with --device-ip."
	  If neither --device-name nor --device-ip are specified, the device-name configured using fconfig is used.`)
	deviceIPFlag := flag.String("device-ip", "", `Serves packages to a device with the given device ip address. Cannot be used with --device-name."
	  If neither --device-name nor --device-ip are specified, the device-name configured using fconfig is used.`)
	sshConfigFlag := flag.String("sshconfig", "", "Use the specified sshconfig file instead of fssh's version.")
	serverMode := flag.String("server-mode", "pm", "Specify the server mode 'pm' or 'ffx'")

	flag.Parse()

	sdk, err := sdkcommon.NewWithDataPath(*dataPathFlag)
	if err != nil {
		log.Fatalf("Could not initialize SDK: %v", err)
	}

	log := logger.NewLogger(level, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "fserve ")
	log.SetFlags(logFlags)
	ctx := logger.WithLogger(context.Background(), log)

	if *helpFlag {
		usage()
		osExit(0)
	}

	deviceConfig, err := sdk.ResolveTargetAddress(*deviceIPFlag, *deviceNameFlag)
	if err != nil {
		log.Fatalf("%v", err)
	}
	log.Infof("Using target address: %v", deviceConfig.DeviceIP)

	// Set the defaults from the SDK if not present.
	if *repoFlag == "" {
		// The device-name is needed to build the repo path.
		if deviceConfig.DeviceName == "" {
			log.Fatalf("Package repository directory cannot be determined. Use --repo-dir to set, or --device-name to select the target device.")
		}
		flag.Set("repo-dir", deviceConfig.PackageRepo)
	}

	if *bucketFlag == "" {
		flag.Set("bucket", deviceConfig.Bucket)
	}

	if *imageFlag == "" {
		flag.Set("image", deviceConfig.Image)
	}

	if *versionFlag == "" {
		flag.Set("version", sdk.GetSDKVersion())
	}

	if *versionFlag == "" && *packageArchiveFlag == "" {
		log.Fatalf("SDK version not known. Use --version to specify it manually.\n")
	}

	// if no deviceIPFlag was given, then get the SSH Port from the configuration.
	// We can't look at the configuration if the ip address was passed in since we don't have the
	// device name which is needed to look up the property.
	sshPort := ""
	if *deviceIPFlag == "" {
		sshPort = deviceConfig.SSHPort
		if err != nil {
			log.Fatalf("Error reading SSH port configuration: %v", err)
		}
		log.Debugf("Using sshport address: %v", sshPort)
		if sshPort == "22" {
			sshPort = ""
		}
	}

	var server packageServer
	switch *serverMode {
	case "pm":
		if *repoPortFlag == "" {
			flag.Set("server-port", deviceConfig.PackagePort)
		}
		if err = killPMServers(ctx, *repoPortFlag); err != nil {
			log.Fatalf("Could not kill existing package servers: %v\n", err)
		}
		if *killFlag {
			osExit(0)
			// this return is needed for tests that overload osExit to not exit.
			return
		}

		server = &pmServer{
			repoPath:      *repoFlag,
			repoPort:      *repoPortFlag,
			name:          *nameFlag,
			targetAddress: deviceConfig.DeviceIP,
			sshConfig:     *sshConfigFlag,
			persist:       *persistFlag,
			privateKey:    *privateKeyFlag,
			sshPort:       sshPort,
		}
	case "ffx":
		if *repoPortFlag != "" {
			log.Errorf(
				"`-server-mode ffx` does not support `-server-port`. "+
					"Instead, to change the repository server port, run: "+
					"`ffx config set repository.server.listen '[::1]:%s' && ffx doctor --restart-daemon`", *repoPortFlag)
			osExit(1)
			// this return is needed for tests that overload osExit to not exit.
			return
		}

		if *killFlag {
			log.Errorf(
				"`-server-mode ffx` does not support `-kill`. " +
					"Instead, to turn off the repository server, run: " +
					"`ffx config set repository.server.listen '' && ffx doctor --restart-daemon`")
			osExit(1)
			// this return is needed for tests that overload osExit to not exit.
			return
		}

		if *privateKeyFlag != "" {
			log.Errorf(
				"`-server-mode ffx` does not support `-private-key` "+
					"Instead, if a specific private key is needed for ffx to communicate with the device, run: "+
					"`ffx config add ssh.priv %s && ffx doctor --restart-daemon`", *privateKeyFlag)
			osExit(1)
			// this return is needed for tests that overload osExit to not exit.
			return
		}

		if *sshConfigFlag != "" {
			log.Errorf("`server-mode ffx` does not support customizing the SSH config settings with `-sshconfig`")
			osExit(1)
			// this return is needed for tests that overload osExit to not exit.
			return
		}

		server = &ffxServer{
			repoPath:      *repoFlag,
			name:          *nameFlag,
			targetAddress: deviceConfig.DeviceIP,
			persist:       *persistFlag,
			sshPort:       sshPort,
		}
	default:
		log.Fatalf("Unknown server mode %v", *serverMode)
	}

	if *cleanFlag {
		if err = cleanPmRepo(ctx, *repoFlag); err != nil {
			log.Warningf("Could not clean up the package repository: %v\n", err)
		}
	} else {
		log.Infof("Using repository: %v (without cleaning, use -clean to clean the package repository first).\n", *repoFlag)
	}

	if *packageArchiveFlag != "" {
		absArchivePath, err := filepath.Abs(*packageArchiveFlag)
		if err != nil {
			log.Fatalf("Could not get absolute path of %v: %v\n", *packageArchiveFlag, err)
		}
		if err = prepareFromArchive(ctx, *repoFlag, absArchivePath); err != nil {
			log.Fatalf("Could not prepare packages: %v\n", err)
		}
	} else if err = prepare(ctx, sdk, *versionFlag, *repoFlag, *bucketFlag, *imageFlag); err != nil {
		log.Fatalf("Could not prepare packages: %v\n", err)
	}
	if *prepareFlag {
		osExit(0)
		// this return is needed for tests that overload osExit to not exit.
		return
	}

	if err := server.startServer(ctx, sdk); err != nil {
		log.Fatalf("Failed to serve packages: %v\n", err)
	}

	if err := server.registerRepository(ctx, sdk); err != nil {
		log.Fatalf("failed to register repository with device: %v", err)
	}

	log.Infof("Successfully started the package server! It is running the background.")

	osExit(0)
}

type packageServer interface {
	startServer(ctx context.Context, sdk sdkProvider) error
	registerRepository(ctx context.Context, sdk sdkProvider) error
}

type pmServer struct {
	repoPath      string
	repoPort      string
	name          string
	targetAddress string
	sshConfig     string
	privateKey    string
	persist       bool
	sshPort       string
}

// startServer starts the `pm serve` command and returns the command object.
func (s *pmServer) startServer(ctx context.Context, sdk sdkProvider) error {
	log := logger.LoggerFromContext(ctx)
	log.Debugf("Starting package server")

	if _, err := startPMServer(sdk, s.repoPath, s.repoPort); err != nil {
		return err
	}

	return nil
}

func (s *pmServer) registerRepository(ctx context.Context, sdk sdkProvider) error {
	return registerPMRepository(ctx, sdk, s.repoPort, s.name, s.targetAddress, s.sshConfig, s.privateKey, s.persist, s.sshPort)
}

type ffxServer struct {
	repoPath      string
	name          string
	targetAddress string
	persist       bool
	sshPort       string
}

func (s *ffxServer) startServer(ctx context.Context, sdk sdkProvider) error {
	// TODO(http://fxbug.dev/83720): We need `ffx_repository=true` until
	// ffx repository has graduated from experimental.
	args := []string{"--config", "ffx_repository=true", "repository",
		"add-from-pm", s.name, s.repoPath}
	logger.Debugf(ctx, "running %v", args)
	if _, err := sdk.RunFFX(args, false); err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			return fmt.Errorf("Error adding repository %v: %w", string(exitError.Stderr), err)
		}
		return err
	}
	return nil
}

func (s *ffxServer) registerRepository(ctx context.Context, sdk sdkProvider) error {
	targetAddress := s.targetAddress
	if s.sshPort != "" {
		targetAddress = fmt.Sprintf("[%s]:%s", targetAddress, s.sshPort)
	}

	// TODO(http://fxbug.dev/83720): We need `ffx_repository=true` until
	// ffx repository has graduated from experimental.
	args := []string{
		"--config", "ffx_repository=true",
		"--target", targetAddress,
		"target", "repository", "register",
		"--repository", s.name,
		"--alias", "fuchsia.com",
	}
	if s.persist {
		args = append(args, "--storage-type", "persistent")
	}
	logger.Debugf(ctx, "running %v", args)
	if _, err := sdk.RunFFX(args, false); err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			return fmt.Errorf("Error adding repository %v: %w", string(exitError.Stderr), err)
		}
		return err
	}
	return nil
}

func usage() {
	fmt.Printf("Usage: %s [options]\n", filepath.Base(os.Args[0]))
	flag.PrintDefaults()
}

// copy or untar a package archive into the repo dir.
func prepareFromArchive(ctx context.Context, repoPath string, archivePath string) error {
	packageDir := filepath.Dir(repoPath)
	log := logger.LoggerFromContext(ctx)

	if sdkcommon.FileExists(archivePath) {
		// untar it.
		log.Debugf("processing package-archive %v", archivePath)
		return processArchiveTarball(ctx, packageDir, archivePath)
	}
	if sdkcommon.DirectoryExists(archivePath) {
		// if it is the same as the dest - a no op.
		if archivePath == packageDir {
			log.Infof("Package archive directory is the same as repo directory, using repo unchanged.")
			return nil
		}
		// Always remove the existing directory when using a directory as the source.

		logger.Debugf(context.Background(), "Removing directory %s\n", packageDir)
		err := os.RemoveAll(packageDir)
		if err != nil {
			logger.Fatalf(ctx, "Could not remove directory %v: %v", packageDir, err)
		}
		// copy it to the repo.
		return copyDir(ctx, archivePath, packageDir)
	}
	return fmt.Errorf("Invalid archive path: %v. Needs to be .tgz file or directory", archivePath)
}

// prepare method -  downloads and untars the packages from GCS if needed.
func prepare(ctx context.Context, sdk sdkcommon.SDKProperties, version string, repoPath string, bucket string, image string) error {
	log := logger.LoggerFromContext(ctx)
	log.Debugf("Preparing package repository at %v for %v:%v@%v", repoPath, bucket, image, version)
	if image == "list" || image == "" {
		if image == "" {
			log.Warningf("Cannot determine image name")
		}
		printValidImages(sdk, version, bucket)
		osExit(1)
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

// cleanPmRepo removes any files in the package repository.
func cleanPmRepo(ctx context.Context, repoDir string) error {
	log := logger.LoggerFromContext(ctx)
	log.Infof("Cleaning the package repository %v.\n", repoDir)

	if _, err := OsStat(repoDir); os.IsNotExist(err) {
		// The repository does not exist and thus is already clean.
		return nil
	}

	args := []string{"-Rf", repoDir}
	cmd := ExecCommand("rm", args...)
	_, err := cmd.Output()
	if err != nil {
		return err
	}
	return nil
}

func killPMServers(ctx context.Context, portNum string) error {
	log := logger.LoggerFromContext(ctx)
	log.Debugf("Killing existing servers")
	// First get all the pm commands for the user
	cmd := ExecCommand("pgrep", "pm")
	output, err := cmd.Output()
	if err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			if string(exitError.Stderr) != "" {
				err = errors.New(string(exitError.Stderr))
			}
			// Special case for some environments which if pgrep does not match
			// the exit code  can be non-zero. In this case check to see if any output
			// was generated and if not, then treat it as if there are no matching processes.
			if exitError.ExitCode() == 1 && len(exitError.Stderr) == 0 && len(output) == 0 {
				return nil
			}
			return fmt.Errorf("Error running pgrep: %v", err)
		}
		return fmt.Errorf("Non exitError encountered: %v", err)
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

	logger.Infof(context.Background(), "No running pm package servers found.\n")
	return nil
}

// startServer starts the `pm serve` command and returns the command object.
// The server is started in the background.
func startPMServer(sdk sdkProvider, repoPath string, repoPort string) (*exec.Cmd, error) {
	toolsDir, err := sdk.GetToolsDir()
	if err != nil {
		return nil, fmt.Errorf("Could not determine tools directory %v", err)
	}
	cmd := filepath.Join(toolsDir, "pm")

	args := []string{"serve"}
	if level != logger.DebugLevel && level != logger.TraceLevel {
		args = append(args, "-q")
	}
	args = append(args, "-repo", repoPath, "-c", "2", "-l", fmt.Sprintf(":%s", repoPort))

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
		log.Debugf("Skipping download, packages tarball exists\n")
	}
	return processArchiveTarball(ctx, packageDir, localImagePath)
}

func processArchiveTarball(ctx context.Context, packageDir string, archiveFile string) error {
	log := logger.LoggerFromContext(ctx)

	// The checksum file contains the output from `md5`. This is used to detect content changes in the packages file.
	isDifferent := false
	checksumFile := filepath.Join(packageDir, "packages.md5")
	md5Hash, err := calculateMD5(archiveFile)
	if err != nil {
		return fmt.Errorf("could not checksum file %v: %v", archiveFile, err)
	}
	if sdkcommon.FileExists(checksumFile) {
		content, err := ioutil.ReadFile(checksumFile)
		if err != nil {
			log.Warningf("Could not read checksum file %v: %v\n", checksumFile, err)
			isDifferent = true
		}
		parts := strings.Fields(string(content))
		isDifferent = isDifferent || md5Hash != parts[0]

		// Look for old file name
		if len(parts) > 1 {
			oldFile := strings.TrimSpace(parts[1])
			if sdkcommon.FileExists(oldFile) && oldFile != archiveFile {
				err = os.Remove(oldFile)
				if err != nil {
					logger.Errorf(ctx, "Could not remove old archive %v: %v\n", oldFile, err)
				}
			} else {
				logger.Debugf(ctx, "%s does not exist or is the same as %s\n", oldFile, archiveFile)
			}
		} else {
			logger.Debugf(ctx, "Could not parse old file name from %v", parts)
		}
	} else {
		logger.Debugf(ctx, "%s does not exist so can't check the checksum\n", checksumFile)
		isDifferent = true
	}
	if isDifferent {
		log.Debugf("Removing directory %s\n", packageDir)
		err := os.RemoveAll(packageDir)
		if err != nil {
			return fmt.Errorf("Could not remove directory %v: %v", packageDir, err)
		}

		if err := os.MkdirAll(packageDir, 0755); err != nil {
			return fmt.Errorf("Could not create directory %v: %v", packageDir, err)
		}
	}
	if !sdkcommon.DirectoryExists(filepath.Join(packageDir, "amber-files")) {
		if err = extractTar(ctx, archiveFile, packageDir); err != nil {
			logger.Fatalf(ctx, "Could not create extract %v into %v: %v", archiveFile, packageDir, err)
		}
	}
	md5File, err := os.Create(checksumFile)
	if err != nil {
		logger.Fatalf(ctx, "Could not write checksum file %v: %v\n", checksumFile, err)
	}
	defer md5File.Close()
	_, err = md5File.WriteString(fmt.Sprintf("%v %v\n", md5Hash, archiveFile))

	return err
}

// registerPMRepository sets the URL for the package server on the target device.
func registerPMRepository(ctx context.Context, sdk sdkProvider, repoPort string, name string,
	targetAddress string, sshConfig string, privateKey string, persist bool, sshPort string) error {

	var (
		err error
		log = logger.LoggerFromContext(ctx)
	)

	if targetAddress == "" {
		return errors.New("target address required")
	}

	log.Debugf("Using target address %v", targetAddress)

	hostIP, err := getHostIPAddressFromTarget(sdk, targetAddress, sshConfig, privateKey, sshPort)
	if err != nil {
		return fmt.Errorf("Could not get host address from target %v: %v", targetAddress, err)
	}
	// A simple heuristic for "is an ipv6 address", URL encase escape
	// the address.
	if strings.Contains(hostIP, ":") {
		hostIP = strings.ReplaceAll(hostIP, "%", "%25")
		hostIP = fmt.Sprintf("[%s]", hostIP)
	}

	var sshArgs []string
	if persist {
		sshArgs = []string{"pkgctl", "repo", "add", "url", "-p", "-n", name,
			fmt.Sprintf("http://%v:%v/config.json", hostIP, repoPort)}
	} else {
		sshArgs = []string{"pkgctl", "repo", "add", "url", "-n", name,
			fmt.Sprintf("http://%v:%v/config.json", hostIP, repoPort)}
	}

	verbose := level == logger.DebugLevel || level == logger.TraceLevel
	if _, err = sdk.RunSSHCommand(targetAddress, sshConfig, privateKey, sshPort, verbose, sshArgs); err != nil {
		return fmt.Errorf("Could not set package server address on device: %v", err)
	}

	ruleTemplate := `'{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"%v","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`
	sshArgs = []string{"pkgctl", "rule", "replace", "json",
		fmt.Sprintf(ruleTemplate, name)}
	if _, err = sdk.RunSSHCommand(targetAddress, sshConfig, privateKey, sshPort, verbose, sshArgs); err != nil {
		return fmt.Errorf("Could not set package url rewriting rules on device: %v", err)
	}

	return nil
}

// getHostIPAddressFromTarget returns the host address reported from the SSH connection on the target device.
func getHostIPAddressFromTarget(sdk sdkProvider, targetAddress string, sshConfig string, privateKey string,
	sshPort string) (string, error) {

	var sshArgs = []string{"echo", "$SSH_CONNECTION"}

	verbose := level == logger.DebugLevel || level == logger.TraceLevel
	connectionString, err := sdk.RunSSHCommand(targetAddress, sshConfig, privateKey,
		sshPort, verbose, sshArgs)

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

func copyDir(ctx context.Context, srcDir string, destDir string) error {
	log := logger.LoggerFromContext(ctx)

	log.Debugf("Copying directory %v to %v", srcDir, destDir)

	srcDirFile, err := os.Open(srcDir)
	if err != nil {
		return fmt.Errorf("Error opening src dir: %v", err)
	}
	defer srcDirFile.Close()

	srcDirStat, err := srcDirFile.Stat()
	if err != nil {
		return err
	}
	if !srcDirStat.IsDir() {
		return fmt.Errorf("Source %v is not a directory", srcDir)
	}

	err = os.MkdirAll(destDir, 0755)
	if err != nil {
		return err
	}

	// Process the contents of srcDir
	contents, err := ioutil.ReadDir(srcDir)
	if err != nil {
		return err
	}
	for _, item := range contents {
		if item.IsDir() {
			// recurse
			err := copyDir(ctx, filepath.Join(srcDir, item.Name()), filepath.Join(destDir, item.Name()))
			if err != nil {
				return err
			}
		} else {
			srcName := filepath.Join(srcDir, item.Name())
			destName := filepath.Join(destDir, item.Name())
			log.Debugf("Copying file %v to %v", srcName, destName)

			srcFile, err := os.Open(srcName)
			if err != nil {
				return err
			}
			defer srcFile.Close()
			destFile, err := os.Create(destName)
			if err != nil {
				return err
			}
			defer destFile.Close()
			// dest is first - which is backwards from cp.
			_, err = io.Copy(destFile, srcFile)
			if err != nil {
				return err
			}
		}
	}
	return nil
}
