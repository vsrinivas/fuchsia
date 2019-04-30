package source

import (
	"encoding/hex"
	"errors"
	"fidl/fuchsia/amber"
	"fidl/fuchsia/pkg"
	"io/ioutil"
	"log"
	"os"
)

type Repository struct {
	source *Source
}

func OpenRepository(config *pkg.RepositoryConfig) (Repository, error) {
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

func (r Repository) Close() {
	r.source.Close()
}

func (r Repository) LocalStoreDir() string {
	return r.source.dir
}

func (r Repository) Update() error {
	return r.source.Update()
}
