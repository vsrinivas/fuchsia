// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package amberctl

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"syscall/zx"
	"time"
	"unicode"

	"amber/urlscope"
	"app/context"
	"fidl/fuchsia/amber"
	fuchsiaio "fidl/fuchsia/io"
	"fidl/fuchsia/pkg"
	"fidl/fuchsia/pkg/rewrite"
	"fidl/fuchsia/space"
	"fidl/fuchsia/update"
)

const usage = `usage: %s <command> [opts]
Commands
    get_up        - get an update for a package
      Options
        -n:      name of the package
        -v:      version of the package to retrieve, if none is supplied any
                 package instance could match
        -m:      merkle root of the package to retrieve, if none is supplied
                 any package instance could match

    add_src       - add a source to the list we can use
        -n: name of the update source (optional, with URL)
        -f: file path or url to a source config file
        -h: SHA256 hash of source config file (optional, with URL)
        -x: [Obsolete] do not disable other active sources (if the provided source is enabled)

    add_repo_cfg  - add a repository config to the set of known repositories, using a source config
        -n: name of the update source (optional, with URL)
        -f: file path or url to a source config file
        -h: SHA256 hash of source config file (optional, with URL)

    rm_src        - remove a source, if it exists, disabling all remaining sources
        -n: name of the update source

    list_srcs     - list the set of sources we can use

    enable_src
        -n: name of the update source
        -x: [Obsolete] do not disable other active sources

    disable_src   - disables all sources

    system_update - check for, download, and apply any available system update

    gc - trigger a garbage collection

    print_state - print go routine state of amber process
`

var (
	fs           = flag.NewFlagSet("default", flag.ExitOnError)
	pkgFile      = fs.String("f", "", "Path to a source config file")
	hash         = fs.String("h", "", "SHA256 hash of source config file (required if -f is a URL, ignored otherwise)")
	name         = fs.String("n", "", "Name of a source or package")
	version      = fs.String("v", "", "Version of a package")
	merkle       = fs.String("m", "", "Merkle root of the desired update.")
	nonExclusive = fs.Bool("x", false, "[Obsolete] When adding or enabling a source, do not disable other sources.")
)

type ErrGetFile string

func NewErrGetFile(str string, inner error) ErrGetFile {
	return ErrGetFile(fmt.Sprintf("%s: %v", str, inner))
}

func (e ErrGetFile) Error() string {
	return string(e)
}

type Services struct {
	amber         *amber.ControlInterface
	resolver      *pkg.PackageResolverInterface
	repoMgr       *pkg.RepositoryManagerInterface
	rewriteEngine *rewrite.EngineInterface
	space         *space.ManagerInterface
	updateManager *update.ManagerInterface
}

func connectToAmber(ctx *context.Context) *amber.ControlInterface {
	req, pxy, err := amber.NewControlInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy
}

func connectToPackageResolver(ctx *context.Context) *pkg.PackageResolverInterface {
	req, pxy, err := pkg.NewPackageResolverInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy
}

func connectToRepositoryManager(ctx *context.Context) *pkg.RepositoryManagerInterface {
	req, pxy, err := pkg.NewRepositoryManagerInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy
}

func connectToRewriteEngine(ctx *context.Context) *rewrite.EngineInterface {
	req, pxy, err := rewrite.NewEngineInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy
}

func connectToSpace(ctx *context.Context) *space.ManagerInterface {
	req, pxy, err := space.NewManagerInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy
}

func connectToUpdateManager(ctx *context.Context) *update.ManagerInterface {
	req, pxy, err := update.NewManagerInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy
}

// upgradeSourceConfig attempts to upgrade an amber.SourceConfig into a pkg.RepositoryConfig
//
// The two config formats are incompatible in various ways:
//
// * repo configs cannot be disabled. amberctl will attempt to preserve a config's disabled bit by
// not configuring a rewrite rule for the source.
//
// * repo configs do not support oauth, network client config options, or polling frequency
// overrides. If present, these options are discarded.
//
// * repo config mirrors do not accept different URLs for the TUF repo and the blobs. Any custom
// blob URL is discarded.
func upgradeSourceConfig(cfg amber.SourceConfig) pkg.RepositoryConfig {
	repoCfg := pkg.RepositoryConfig{
		RepoUrl:        repoUrlForId(cfg.Id),
		RepoUrlPresent: true,
	}

	mirror := pkg.MirrorConfig{
		MirrorUrl:        cfg.RepoUrl,
		MirrorUrlPresent: true,
		Subscribe:        cfg.Auto,
		SubscribePresent: true,
	}
	if cfg.BlobKey != nil {
		var blobKey pkg.RepositoryBlobKey
		blobKey.SetAesKey(cfg.BlobKey.Data[:])
		mirror.SetBlobKey(blobKey)
	}
	repoCfg.SetMirrors([]pkg.MirrorConfig{mirror})

	for _, key := range cfg.RootKeys {
		if key.Type != "ed25519" {
			continue
		}

		var rootKey pkg.RepositoryKeyConfig
		bytes, err := hex.DecodeString(key.Value)
		if err != nil {
			continue
		}
		rootKey.SetEd25519Key(bytes)

		repoCfg.RootKeys = append(repoCfg.RootKeys, rootKey)
		repoCfg.RootKeysPresent = true
	}

	return repoCfg
}

var invalidHostnameCharsPattern = regexp.MustCompile("[^a-zA-Z0-9_-]")

func sanitizeId(id string) string {
	return invalidHostnameCharsPattern.ReplaceAllString(id, "_")
}

func repoUrlForId(id string) string {
	return fmt.Sprintf("fuchsia-pkg://%s", id)
}

func rewriteRuleForId(id string) rewrite.Rule {
	var rule rewrite.Rule
	rule.SetLiteral(rewrite.LiteralRule{
		HostMatch:             "fuchsia.com",
		HostReplacement:       id,
		PathPrefixMatch:       "/",
		PathPrefixReplacement: "/",
	})
	return rule
}

func replaceDynamicRewriteRules(rewriteEngine *rewrite.EngineInterface, rule rewrite.Rule) error {
	return doRewriteRuleEditTransaction(rewriteEngine, func(transaction *rewrite.EditTransactionInterface) error {
		if err := transaction.ResetAll(); err != nil {
			return fmt.Errorf("fuchsia.pkg.rewrite.EditTransaction.ResetAll IPC encountered an error: %s", err)
		}

		s, err := transaction.Add(rule)
		if err != nil {
			return fmt.Errorf("fuchsia.pkg.rewrite.EditTransaction.Add IPC encountered an error: %s", err)
		}
		status := zx.Status(s)
		if status != zx.ErrOk {
			return fmt.Errorf("unable to add rewrite rule: %s", status)
		}

		return nil
	})
}

func removeAllDynamicRewriteRules(rewriteEngine *rewrite.EngineInterface) error {
	return doRewriteRuleEditTransaction(rewriteEngine, func(transaction *rewrite.EditTransactionInterface) error {
		if err := transaction.ResetAll(); err != nil {
			return fmt.Errorf("fuchsia.pkg.rewrite.EditTransaction.ResetAll IPC encountered an error: %s", err)
		}

		return nil
	})
}

// doRewriteRuleEditTransaction executes a rewrite rule edit transaction using
// the provided callback, retrying on data races a few times before giving up.
func doRewriteRuleEditTransaction(rewriteEngine *rewrite.EngineInterface, cb func(*rewrite.EditTransactionInterface) error) error {
	for i := 0; i < 10; i++ {
		err, status := func() (error, zx.Status) {
			var status zx.Status
			req, transaction, err := rewrite.NewEditTransactionInterfaceRequest()
			if err != nil {
				return fmt.Errorf("creating edit transaction: %s", err), status
			}
			defer transaction.Close()
			if err := rewriteEngine.StartEditTransaction(req); err != nil {
				return fmt.Errorf("fuchsia.pkg.rewrite.Engine IPC encountered an error: %s", err), status
			}

			if err := cb(transaction); err != nil {
				return err, status
			}

			s, err := transaction.Commit()
			if err != nil {
				return fmt.Errorf("fuchsia.pkg.rewrite.EditTransaction.Commit IPC encountered an error: %s", err), status
			}
			return nil, zx.Status(s)
		}()
		if err != nil {
			return err
		}
		switch status {
		case zx.ErrOk:
			return nil
		case zx.ErrUnavailable:
			continue
		default:
			return fmt.Errorf("unexpected error while committing rewrite rule transaction: %s", status)
		}
	}

	return fmt.Errorf("unable to commit rewrite rule changes")
}

func addSource(services Services, repoOnly bool) error {
	if len(*pkgFile) == 0 {
		return fmt.Errorf("a url or file path (via -f) are required")
	}

	var source io.Reader
	url, err := url.Parse(*pkgFile)
	isURL := false
	if err == nil && url.IsAbs() {
		isURL = true
		var expectedHash []byte
		hash := strings.TrimSpace(*hash)
		if len(hash) != 0 {

			var err error
			expectedHash, err = hex.DecodeString(hash)
			if err != nil {
				return fmt.Errorf("hash is not a hex encoded string: %v", err)
			}
		}

		resp, err := http.Get(*pkgFile)
		if err != nil {
			return NewErrGetFile("failed to GET file", err)
		}
		defer resp.Body.Close()
		if resp.StatusCode != 200 {
			io.Copy(ioutil.Discard, resp.Body)
			return fmt.Errorf("GET response: %v", resp.Status)
		}

		body, err := ioutil.ReadAll(resp.Body)
		if err != nil {
			return fmt.Errorf("failed to read file body: %v", err)
		}

		if len(expectedHash) != 0 {
			hasher := sha256.New()
			hasher.Write(body)
			actualHash := hasher.Sum(nil)

			if !bytes.Equal(expectedHash, actualHash) {
				return fmt.Errorf("hash of config file does not match!")
			}
		}

		source = bytes.NewReader(body)

	} else {
		f, err := os.Open(*pkgFile)
		if err != nil {
			return fmt.Errorf("failed to open file: %v", err)
		}
		defer f.Close()

		source = f
	}

	var cfg amber.SourceConfig
	if err := json.NewDecoder(source).Decode(&cfg); err != nil {
		return fmt.Errorf("failed to parse source config: %v", err)
	}

	if *name != "" {
		cfg.Id = *name
	} else {
		cfg.Id = sanitizeId(cfg.Id)
	}

	// Update the host segment of the URL with the original if it appears to have
	// only been de-scoped, so that link-local configurations retain ipv6 scopes.
	if isURL {
		if remote, err := url.Parse(cfg.RepoUrl); err == nil {
			if u := urlscope.Rescope(url, remote); u != nil {
				cfg.RepoUrl = u.String()
			}
		}
		if remote, err := url.Parse(cfg.BlobRepoUrl); err == nil {
			if u := urlscope.Rescope(url, remote); u != nil {
				cfg.BlobRepoUrl = u.String()
			}
		}
	}

	if cfg.BlobRepoUrl == "" {
		cfg.BlobRepoUrl = filepath.Join(cfg.RepoUrl, "blobs")
	}

	repoCfg := upgradeSourceConfig(cfg)
	s, err := services.repoMgr.Add(repoCfg)
	if err != nil {
		return fmt.Errorf("fuchsia.pkg.RepositoryManager IPC encountered an error: %s", err)
	}
	status := zx.Status(s)
	if !(status == zx.ErrOk || status == zx.ErrAlreadyExists) {
		return fmt.Errorf("unable to register source with RepositoryManager: %s", status)
	}

	// Nothing currently registers sources in a disabled state, but make a best effort attempt
	// to try to prevent the source from being used anyway by only configuring a mapping of
	// fuchsia.com to this source if it is enabled. Note that this doesn't prevent resolving a
	// package using this config's id explicitly or calling an amber source config
	// "fuchsia.com".
	if !repoOnly && isSourceConfigEnabled(&cfg) {
		rule := rewriteRuleForId(cfg.Id)
		if err := replaceDynamicRewriteRules(services.rewriteEngine, rule); err != nil {
			return err
		}
	}

	return nil
}

func rmSource(services Services) error {
	name := strings.TrimSpace(*name)
	if name == "" {
		return fmt.Errorf("no source id provided")
	}

	// Since modifications to RepositoryManager and rewrite.Engine aren't atomic and amberctl
	// could be interrupted or encounter an error during any step, unregister the rewrite rule
	// before removing the repo config to prevent a dangling rewrite rule to a repo that no
	// longer exists.
	if err := removeAllDynamicRewriteRules(services.rewriteEngine); err != nil {
		return err
	}

	s, err := services.repoMgr.Remove(repoUrlForId(name))
	if err != nil {
		return fmt.Errorf("fuchsia.pkg.RepositoryManager IPC encountered an error: %s", err)
	}
	zxStatus := zx.Status(s)
	if !(zxStatus == zx.ErrOk || zxStatus == zx.ErrNotFound) {
		return fmt.Errorf("unable to remove source from RepositoryManager: %s", zxStatus)
	}

	return nil
}

func getUp(r *pkg.PackageResolverInterface) error {
	if *name == "" {
		return fmt.Errorf("no source id provided")
	}

	var err error
	for i := 0; i < 3; i++ {
		err = getUpdateComplete(r, *name, version, merkle)
		if err == nil {
			break
		}
		fmt.Printf("Update failed with error %s, retrying...\n", err)
		time.Sleep(2 * time.Second)
	}
	return err
}

func listSources(r *pkg.RepositoryManagerInterface) error {
	req, iter, err := pkg.NewRepositoryIteratorInterfaceRequest()
	if err != nil {
		return err
	}
	defer iter.Close()
	if err := r.List(req); err != nil {
		return err
	}

	for {
		repos, err := iter.Next()
		if err != nil {
			return err
		}
		if len(repos) == 0 {
			break
		}

		for _, repo := range repos {
			encoder := json.NewEncoder(os.Stdout)
			encoder.SetIndent("", "    ")
			if err := encoder.Encode(repo); err != nil {
				fmt.Printf("failed to encode source into json: %s\n", err)
				return err
			}
		}
	}

	return nil
}

func isSourceConfigEnabled(cfg *amber.SourceConfig) bool {
	if cfg.StatusConfig == nil {
		return true
	}
	return cfg.StatusConfig.Enabled
}

func do(services Services) int {
	switch os.Args[1] {
	case "get_up":
		if err := getUp(services.resolver); err != nil {
			log.Printf("error getting an update: %s", err)
			return 1
		}
	case "add_repo_cfg":
		if err := addSource(services, true); err != nil {
			log.Printf("error adding repo: %s", err)
			if _, ok := err.(ErrGetFile); ok {
				return 2
			} else {
				return 1
			}
		}
	case "add_src":
		if err := addSource(services, false); err != nil {
			log.Printf("error adding source: %s", err)
			if _, ok := err.(ErrGetFile); ok {
				return 2
			} else {
				return 1
			}
		}
	case "rm_src":
		if err := rmSource(services); err != nil {
			log.Printf("error removing source: %s", err)
			return 1
		}
	case "list_srcs":
		if err := listSources(services.repoMgr); err != nil {
			log.Printf("error listing sources: %s", err)
			return 1
		}
	case "check":
		log.Printf("%q not yet supported\n", os.Args[1])
		return 1
	case "system_update":
		result, err := services.updateManager.CheckNow(
			update.Options{
				Initiator: update.InitiatorUser, InitiatorPresent: true},
			update.MonitorInterfaceRequest{zx.Channel(zx.HandleInvalid)})
		if err != nil {
			log.Printf("error checking for system update: %s", err)
			return 1
		}

		switch result {
		case update.CheckStartedResultStarted:
			fmt.Printf("triggered a system update check\n")
			return 0
		case update.CheckStartedResultInProgress:
			fmt.Printf("system update check already in progress\n")
			return 0
		default:
			fmt.Printf("system update check failed: %s", result)
			return 1
		}
	case "enable_src":
		if *name == "" {
			log.Printf("Error enabling source: no source id provided")
			return 1
		}
		err := replaceDynamicRewriteRules(services.rewriteEngine, rewriteRuleForId(*name))
		if err != nil {
			log.Printf("Error configuring rewrite rules: %s", err)
			return 1
		}
		fmt.Printf("Source %q enabled\n", *name)
	case "disable_src":
		if *name != "" {
			log.Printf("\"name\" parameter is now ignored: disabling all sources.\n"+
				"To enable a specific source, use 'amberctl enable_src -n %q'", *name)
		}
		err := removeAllDynamicRewriteRules(services.rewriteEngine)
		if err != nil {
			log.Printf("Error configuring rewrite rules: %s", err)
			return 1
		}
		fmt.Printf("Source %q disabled\n", *name)
	case "gc":
		res, err := services.space.Gc()
		if err != nil {
			log.Printf("Error collecting garbage: %s", err)
			return 1
		}
		if res.Which() == space.ManagerGcResultErr {
			log.Printf("Error collecting garbage: %s", res.Err)
			return 1
		}
		log.Printf("Garbage collection complete. See logs for details.")
	case "print_state":
		if err := filepath.Walk("/hub", func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}
			switch name := info.Name(); name {
			case "goroutines":
				if f, err := os.Open(path); err != nil {
					return err
				} else {
					_, err := io.Copy(os.Stdout, f)
					return err
				}
			case "hub", "c", "r", "amber.cmx", "out", "debug":
				return nil
			default:
				if info.IsDir() {
					for _, r := range name {
						if !unicode.IsDigit(r) {
							return filepath.SkipDir
						}
					}
				}
				return nil
			}
		}); err != nil {
			log.Printf("Error printing process state: %s", err)
			return 1
		}
	default:

		fmt.Printf("Error, %q is not a recognized command\n", os.Args[1])
		fmt.Printf(usage, filepath.Base(os.Args[0]))
		return -1
	}

	return 0
}

func Main() {
	if len(os.Args) < 2 {
		fmt.Println("Error: no command provided")
		fmt.Printf(usage, filepath.Base(os.Args[0]))
		os.Exit(-1)
	}

	fs.Parse(os.Args[2:])

	if *name != "" {
		*name = sanitizeId(*name)
	}

	if *nonExclusive {
		fmt.Println(`Warning: -x is no longer supported.`)
	}

	ctx := context.CreateFromStartupInfo()

	var services Services

	services.amber = connectToAmber(ctx)
	defer services.amber.Close()

	services.resolver = connectToPackageResolver(ctx)
	defer services.resolver.Close()

	services.repoMgr = connectToRepositoryManager(ctx)
	defer services.repoMgr.Close()

	services.rewriteEngine = connectToRewriteEngine(ctx)
	defer services.rewriteEngine.Close()

	services.space = connectToSpace(ctx)
	defer services.space.Close()

	services.updateManager = connectToUpdateManager(ctx)
	defer services.updateManager.Close()

	os.Exit(do(services))
}

type ErrDaemon string

func NewErrDaemon(str string) ErrDaemon {
	return ErrDaemon(fmt.Sprintf("amberctl: daemon error: %s", str))
}

func (e ErrDaemon) Error() string {
	return string(e)
}

type resolveResult struct {
	status zx.Status
	err    error
}

func getUpdateComplete(r *pkg.PackageResolverInterface, name string, version *string, merkle *string) error {
	pkgUri := fmt.Sprintf("fuchsia-pkg://fuchsia.com/%s", name)
	if *version != "" {
		pkgUri = fmt.Sprintf("%s/%s", pkgUri, *version)
	}
	if *merkle != "" {
		pkgUri = fmt.Sprintf("%s?hash=%s", pkgUri, *merkle)
	}

	selectors := []string{}
	updatePolicy := pkg.UpdatePolicy{}

	dirReq, dirPxy, err := fuchsiaio.NewDirectoryInterfaceRequest()
	if err != nil {
		return err
	}
	defer dirPxy.Close()

	ch := make(chan resolveResult)
	go func() {
		status, err := r.Resolve(pkgUri, selectors, updatePolicy, dirReq)
		ch <- resolveResult{
			status: zx.Status(status),
			err:    err,
		}
	}()

	for {
		select {
		case result := <-ch:
			if result.err != nil {
				return fmt.Errorf("error getting update: %s", result.err)
			}
			if result.status != zx.ErrOk {
				return fmt.Errorf("fetch: Resolve status: %s", result.status)
			}
			return nil
		case <-time.After(3 * time.Second):
			log.Println("Awaiting response...")
		}
	}
}
