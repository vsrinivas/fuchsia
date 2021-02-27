module go.fuchsia.dev/fuchsia

go 1.16

replace (
	cloud.google.com/go => ./third_party/golibs/github.com/googleapis/google-cloud-go
	github.com/creack/pty => ./third_party/golibs/github.com/creack/pty
	// github.com/dustin/go-humanize => ./third_party/golibs/github.com/dustin/go-humanize
	github.com/flynn/go-tuf => ./third_party/golibs/github.com/flynn/go-tuf
	github.com/fsnotify/fsnotify => ./third_party/golibs/github.com/fsnotify/fsnotify
	github.com/go-yaml/yaml => ./third_party/golibs/github.com/go-yaml/yaml
	github.com/golang/protobuf => ./third_party/golibs/github.com/golang/protobuf
	github.com/google/go-cmp => ./third_party/golibs/github.com/google/go-cmp
	github.com/google/shlex => ./third_party/golibs/github.com/google/shlex
	github.com/google/subcommands => ./third_party/golibs/github.com/google/subcommands
	// github.com/kr/fs => ./third_party/golibs/github.com/kr/fs
	github.com/kr/pretty => ./third_party/golibs/github.com/kr/pretty
	// github.com/pkg/errors => ./third_party/golibs/github.com/pkg/errors
	github.com/pkg/sftp => ./third_party/golibs/github.com/pkg/sftp
	github.com/spf13/pflag => ./third_party/golibs/github.com/spf13/pflag
	// go.chromium.org/luci => ./third_party/luci-go
	go.uber.org/multierr => ./third_party/golibs/go.uber.org/multierr
	golang.org/x/crypto => ./third_party/golibs/golang.org/x/crypto
	golang.org/x/net => ./third_party/golibs/golang.org/x/net
	golang.org/x/sync => ./third_party/golibs/golang.org/x/sync
	golang.org/x/sys => ./third_party/golibs/golang.org/x/sys
	golang.org/x/time => ./third_party/golibs/golang.org/x/time
	golang.org/x/xerrors => ./third_party/golibs/golang.org/x/xerrors
	google.golang.org/grpc => ./third_party/golibs/github.com/grpc/grpc-go
	google.golang.org/protobuf => ./third_party/golibs/github.com/protocolbuffers/protobuf-go
	gvisor.dev/gvisor => ./third_party/golibs/gvisor.dev/gvisor
)

require (
	cloud.google.com/go/storage v1.13.0
	github.com/creack/pty v0.0.0-00010101000000-000000000000
	github.com/dustin/go-humanize v1.0.0
	github.com/flynn/go-tuf v0.0.0-00010101000000-000000000000
	github.com/fsnotify/fsnotify v1.4.7
	github.com/go-yaml/yaml v0.0.0-00010101000000-000000000000
	github.com/golang/glog v0.0.0-20160126235308-23def4e6c14b
	github.com/golang/protobuf v1.4.3
	github.com/google/go-cmp v0.5.4
	github.com/google/shlex v0.0.0-00010101000000-000000000000
	github.com/google/subcommands v1.0.2-0.20190508160503-636abe8753b8
	github.com/julienschmidt/httprouter v1.3.0 // indirect
	github.com/klauspost/compress v1.11.8 // indirect
	github.com/kr/fs v0.1.0
	github.com/kr/pretty v0.1.0
	github.com/pkg/sftp v0.0.0-00010101000000-000000000000
	github.com/smartystreets/assertions v1.2.0 // indirect
	github.com/smartystreets/goconvey v1.6.4 // indirect
	github.com/spf13/pflag v1.0.5
	github.com/tent/canonical-json-go v0.0.0-20130607151641-96e4ba3a7613 // indirect
	go.chromium.org/luci v0.0.0-20210226174505-2499695eadf2
	go.uber.org/multierr v1.6.0
	golang.org/x/crypto v0.0.0-20200220183623-bac4c82f6975
	golang.org/x/net v0.0.0-20201224014010-6772e930b67b
	golang.org/x/sync v0.0.0-20201207232520-09787c993a3a
	golang.org/x/sys v0.0.0-20210124154548-22da62e12c0c
	google.golang.org/grpc v1.36.0-dev.0.20210208035533-9280052d3665
	google.golang.org/protobuf v1.25.1-0.20201020201750-d3470999428b
	gvisor.dev/gvisor v0.0.0-00010101000000-000000000000
)
