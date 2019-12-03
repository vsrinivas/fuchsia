package source

import (
	"encoding/hex"
	"errors"
	"fidl/fuchsia/amber"
	"fidl/fuchsia/pkg"
	"fmt"
	"io/ioutil"
	"log"
	"os"
)

type Repository struct {
	source *Source
}

func convertRepositoryConfig(config *pkg.RepositoryConfig) (*amber.SourceConfig, error) {
	if len(config.Mirrors) == 0 {
		return nil, errors.New("There must be at least one mirror")
	}
	// Hard code using the first mirror
	var mirrorConfig *pkg.MirrorConfig = &config.Mirrors[0]
	if !mirrorConfig.HasMirrorUrl() {
		return nil, errors.New("mirror_url is required")
	}
	var repoUrl string = mirrorConfig.MirrorUrl
	var blobRepoUrl string = mirrorConfig.BlobMirrorUrl
	var rootKeys []amber.KeyConfig
	for _, key := range config.RootKeys {
		if key.Which() != pkg.RepositoryKeyConfigEd25519Key {
			return nil, errors.New("Only ed25519 root keys are supported")
		}
		rootKeys = append(rootKeys, amber.KeyConfig{Type: "ed25519", Value: hex.EncodeToString(key.Ed25519Key)})
		log.Printf("DEBUG: added keys: %v", rootKeys)
	}

	var blobKey *amber.BlobEncryptionKey
	if mirrorConfig.HasBlobKey() {
		if mirrorConfig.BlobKey.Which() != pkg.RepositoryBlobKeyAesKey {
			return nil, errors.New("Only aes blob keys are supported")
		}
		data := mirrorConfig.BlobKey.AesKey
		if len(data) != 32 {
			return nil, errors.New("Blob keys must be exactly 32 bytes long")
		}
		var arr [32]uint8
		copy(arr[:], data)
		blobKey = &amber.BlobEncryptionKey{
			Data: arr,
		}
	}

	var ratePeriod int32
	if mirrorConfig.Subscribe {
		ratePeriod = 60
	}

	cfg := &amber.SourceConfig{
		RepoUrl:      repoUrl,
		BlobRepoUrl:  blobRepoUrl,
		RootKeys:     rootKeys,
		StatusConfig: &amber.StatusConfig{Enabled: true},
		Auto:         mirrorConfig.Subscribe,
		BlobKey:      blobKey,
		RatePeriod:   ratePeriod,
	}
	return cfg, nil
}

func OpenRepository(config *pkg.RepositoryConfig) (Repository, error) {
	result := Repository{source: nil}

	cfg, err := convertRepositoryConfig(config)
	if err != nil {
		return result, err
	}
	result.source, err = openTemporarySource(cfg)
	if err != nil {
		return result, err
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

func (r Repository) MerkleFor(name, version, merkle string) (string, int64, error) {
	// Temporary-ish solution to avoid failing/pulling incorrectly updated
	// packages. We need an index into TUF metadata in order to capture appropriate
	// length information.
	if len(merkle) == 64 {
		return merkle, -1, nil
	}

	src := r.source

	if err := src.UpdateIfStale(); err != nil {
		log.Printf("repo: could not update TUF metadata: %s", err)
		// try to continue anyway
	}

	m, l, err := src.MerkleFor(name, version)
	if err != nil {
		if err == ErrUnknownPkg {
			return "", 0, fmt.Errorf("merkle not found for package %s/%s", name, version)
		}
		return "", 0, fmt.Errorf("error finding merkle for package %s/%s: %v", name, version, err)
	}
	return m, l, nil
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
