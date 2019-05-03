package source

import (
	"amber/atonce"
	"encoding/hex"
	"errors"
	"fidl/fuchsia/amber"
	"fidl/fuchsia/pkg"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"syscall/zx"
)

type Repository struct {
	source    *Source
	installer *packageInstaller
}

func OpenRepository(config *pkg.RepositoryConfig, pkgInstallDir string, blobInstallDir string, pkgNeedsDir string) (Repository, error) {
	result := Repository{source: nil}
	if len(config.Mirrors) == 0 {
		return result, errors.New("There must be at least one mirror")
	}
	// Hard code using the first mirror
	var mirrorConfig *pkg.MirrorConfig = &config.Mirrors[0]
	if !mirrorConfig.HasMirrorUrl() {
		return result, errors.New("mirror_url is required")
	}
	var repoUrl string = mirrorConfig.MirrorUrl
	var rootKeys []amber.KeyConfig
	for _, key := range config.RootKeys {
		if key.Which() != pkg.RepositoryKeyConfigEd25519Key {
			return result, errors.New("Only ed25519 root keys are supported")
		}
		rootKeys = append(rootKeys, amber.KeyConfig{Type: "ed25519", Value: hex.EncodeToString(key.Ed25519Key)})
		log.Printf("DEBUG: added keys: %v", rootKeys)
	}

	var blobKey *amber.BlobEncryptionKey
	if mirrorConfig.HasBlobKey() {
		if mirrorConfig.BlobKey.Which() != pkg.RepositoryBlobKeyAesKey {
			return result, errors.New("Only aes blob keys are supported")
		}
		data := mirrorConfig.BlobKey.AesKey
		if len(data) != 32 {
			return result, errors.New("Blob keys must be exactly 32 bytes long")
		}
		var arr [32]uint8
		copy(arr[:], data)
		blobKey = &amber.BlobEncryptionKey{
			Data: arr,
		}
	}

	cfg := &amber.SourceConfig{
		RepoUrl:      repoUrl,
		RootKeys:     rootKeys,
		StatusConfig: &amber.StatusConfig{Enabled: true},
		Auto:         mirrorConfig.Subscribe,
		BlobKey:      blobKey,
	}
	var err error
	result.source, err = openTemporarySource(cfg)
	if err != nil {
		return result, err
	}
	result.installer = &packageInstaller{
		pkgInstallDir:  pkgInstallDir,
		pkgNeedsDir:    pkgNeedsDir,
		blobInstallDir: blobInstallDir,
		fetcher:        &result,
	}
	return result, nil
}

func openTemporarySource(cfg *amber.SourceConfig) (*Source, error) {
	os.MkdirAll("/tmp/store", 0700)
	store, err := ioutil.TempDir("/tmp/store", "repo")
	cfg.Id = store
	if err != nil {
		log.Printf("failed to create TempDir: %v", err)
		return nil, err
	}
	src, err := NewSource(store, cfg)
	if err != nil {
		log.Printf("failed to create TUF source: %v: %s", cfg.Id, err)
		return nil, err
	}

	// Save the config.
	if err := src.Save(); err != nil {
		log.Printf("failed to save TUF config %v: %s", cfg.Id, err)
		src.Close()
		return nil, err
	}
	src.Start()

	return src, nil
}

func (r Repository) fetchInto(merkle string, length int64, outputDir string) error {
	return atonce.Do("fetchInto", merkle, func() error {
		var err error
		err = r.source.FetchInto(merkle, length, outputDir)
		if err == nil || os.IsExist(err) {
			return err
		}
		if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrNoSpace {
			// TODO: Notify for out of space?
			return err
		}
		return err
	})
}

type blobFetcher interface {
	fetchInto(merkle string, length int64, outputDir string) error
}

type packageInstaller struct {
	pkgInstallDir  string
	pkgNeedsDir    string
	blobInstallDir string
	fetcher        blobFetcher
}

func (i packageInstaller) GetPkg(merkle string, length int64) error {
	err := i.fetcher.fetchInto(merkle, length, i.pkgInstallDir)
	if os.IsExist(err) {
		return nil
	}

	if err != nil {
		log.Printf("error fetching pkg %q: %s", merkle, err)
		return err
	}

	needsDir, err := os.Open(filepath.Join(i.pkgNeedsDir, merkle))
	if os.IsNotExist(err) {
		// Package is fully installed already
		return nil
	}
	defer needsDir.Close()

	neededBlobs, err := needsDir.Readdirnames(-1)
	if err != nil {
		return err
	}
	for len(neededBlobs) > 0 {
		for _, blob := range neededBlobs {
			// TODO(raggi): switch to using the needs paths for install
			err := i.fetcher.fetchInto(blob, -1, i.blobInstallDir)
			if err != nil {
				return err
			}
		}

		neededBlobs, err = needsDir.Readdirnames(-1)
		if err != nil {
			return err
		}
	}
	return err
}

func (r Repository) MerkleFor(name, version, merkle string) (string, int64, error) {
	// Temporary-ish solution to avoid failing/pulling incorrectly updated
	// packages. We need an index into TUF metadata in order to capture appropriate
	// length information.
	if len(merkle) == 64 {
		return merkle, -1, nil
	}

	src := r.source
	m, l, err := src.MerkleFor(name, version)
	if err != nil {
		if err == ErrUnknownPkg {
			return "", 0, fmt.Errorf("merkle not found for package %s/%s", name, version)
		}
		return "", 0, fmt.Errorf("error finding merkle for package %s/%s: %v", name, version, err)
	}
	return m, l, nil
}

func (r Repository) GetUpdateComplete(name string, ver, mer *string) (string, zx.Status, error) {
	if len(name) == 0 {
		log.Printf("getupdatecomplete: invalid arguments: empty name")
		return "", zx.ErrInvalidArgs, errors.New(zx.ErrInvalidArgs.String())
	}

	var (
		version string
		merkle  string
	)

	if ver != nil {
		version = *ver
	}
	if mer != nil {
		merkle = *mer
	}

	r.source.UpdateIfStale()

	root, length, err := r.MerkleFor(name, version, merkle)
	if err != nil {
		log.Printf("repo: could not get update for %s: %s", filepath.Join(name, version, merkle), err)
		return "", zx.ErrInternal, err
	}

	if _, err := os.Stat(filepath.Join("/pkgfs/versions", root)); err == nil {
		return root, zx.ErrOk, nil
	}

	log.Printf("repo: get update: %s", filepath.Join(name, version, merkle))

	err = r.installer.GetPkg(root, length)
	if os.IsExist(err) {
		log.Printf("repo: %s already installed", filepath.Join(name, version, root))
		// signal success to the client
		return root, zx.ErrOk, nil
	}
	if err != nil {
		log.Printf("repo: error downloading package: %s", err)
		if e, ok := err.(*zx.Error); ok && e.Status == zx.ErrNoSpace {
			return "", e.Status, e
		}
		return "", zx.ErrInternal, err
	}

	return root, zx.ErrOk, nil
}

func (r Repository) Close() {
	r.source.Close()
}

func (r Repository) LocalStoreDir() string {
	return r.source.dir
}

func (r Repository) Update() error {
	return r.source.Update()
}
