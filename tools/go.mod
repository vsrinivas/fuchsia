module go.fuchsia.dev/fuchsia/tools

go 1.15

require (
	cloud.google.com/go/storage v1.8.0
	github.com/creack/pty v1.1.11
	github.com/go-yaml/yaml v2.1.0+incompatible
	github.com/golang/glog v0.0.0-20160126235308-23def4e6c14b
	github.com/golang/protobuf v1.4.2
	github.com/google/go-cmp v0.5.4
	github.com/google/shlex v0.0.0-20191202100458-e7afc7fbc510
	github.com/google/subcommands v1.2.0
	github.com/klauspost/compress v1.11.7 // indirect
	github.com/kr/fs v0.1.0
	github.com/kr/pretty v0.1.0
	github.com/pkg/sftp v1.12.0
	github.com/spf13/pflag v1.0.5
	go.chromium.org/luci v0.0.0-20210209062837-a3b46505d9a0
	go.fuchsia.dev/fuchsia/src v0.0.0-20200821151753-3226fa91b98e
	golang.org/x/crypto v0.0.0-20200820211705-5c72a883971a
	golang.org/x/net v0.0.0-20200822124328-c89045814202
	golang.org/x/sync v0.0.0-20200625203802-6e8e738ad208
	golang.org/x/sys v0.0.0-20200828081204-131dc92a58d5
	google.golang.org/grpc v1.31.1
	google.golang.org/protobuf v1.25.1-0.20201020201750-d3470999428b
)

replace (
	cloud.google.com/go => ../third_party/golibs/github.com/googleapis/google-cloud-go
	github.com/creack/pty => ../third_party/golibs/github.com/creack/pty
	github.com/flynn/go-tuf => ../third_party/golibs/github.com/flynn/go-tuf
	github.com/go-yaml/yaml => ../third_party/golibs/github.com/go-yaml/yaml
	github.com/golang/protobuf => ../third_party/golibs/github.com/golang/protobuf
	github.com/google/go-cmp => ../third_party/golibs/github.com/google/go-cmp
	github.com/google/subcommands => ../third_party/golibs/github.com/google/subcommands
	github.com/pkg/sftp => ../third_party/golibs/github.com/pkg/sftp
	github.com/spf13/pflag => ../third_party/golibs/github.com/spf13/pflag
	go.fuchsia.dev/fuchsia/garnet => ../garnet
	go.fuchsia.dev/fuchsia/src => ../src
	golang.org/x/crypto => ../third_party/golibs/golang.org/x/crypto
	golang.org/x/net => ../third_party/golibs/golang.org/x/net
	golang.org/x/sync => ../third_party/golibs/golang.org/x/sync
	golang.org/x/sys => ../third_party/golibs/golang.org/x/sys
	google.golang.org/grpc => ../third_party/golibs/github.com/grpc/grpc-go
	google.golang.org/protobuf => ../third_party/golibs/github.com/protocolbuffers/protobuf-go
)
