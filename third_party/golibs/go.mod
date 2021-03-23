module go.fuchsia.dev/fuchsia

go 1.16

// We forked this one.
replace github.com/flynn/go-tuf => fuchsia.googlesource.com/third_party/go-tuf.git v0.0.0-20200826175457-a90d70916791

require (
	cloud.google.com/go/storage v1.14.0
	github.com/creack/pty v1.1.11
	github.com/dustin/go-humanize v1.0.0
	github.com/flynn/go-tuf v0.0.0-20201230183259-aee6270feb55
	github.com/fsnotify/fsnotify v1.4.9
	github.com/golang/glog v0.0.0-20160126235308-23def4e6c14b
	github.com/golang/protobuf v1.5.1
	github.com/google/btree v1.0.1 // indirect
	github.com/google/go-cmp v0.5.5
	github.com/google/shlex v0.0.0-20191202100458-e7afc7fbc510
	github.com/google/subcommands v1.2.0
	github.com/kr/fs v0.1.0
	github.com/kr/pretty v0.2.1
	github.com/kr/text v0.2.0 // indirect
	github.com/pkg/sftp v1.13.0
	github.com/spf13/pflag v1.0.5
	github.com/tent/canonical-json-go v0.0.0-20130607151641-96e4ba3a7613 // indirect
	go.uber.org/multierr v1.6.0
	golang.org/x/crypto v0.0.0-20210317152858-513c2a44f670
	golang.org/x/mod v0.4.2 // indirect
	golang.org/x/net v0.0.0-20210316092652-d523dce5a7f4
	golang.org/x/sync v0.0.0-20210220032951-036812b2e83c
	golang.org/x/sys v0.0.0-20210319071255-635bc2c9138d
	golang.org/x/time v0.0.0-20210220033141-f8bda1e9f3ba // indirect
	gonum.org/v1/gonum v0.9.0
	google.golang.org/api v0.42.0 // indirect
	google.golang.org/genproto v0.0.0-20210322173543-5f0e89347f5a // indirect
	google.golang.org/grpc v1.36.0
	google.golang.org/grpc/cmd/protoc-gen-go-grpc v1.1.0
	google.golang.org/protobuf v1.26.0
	gopkg.in/yaml.v2 v2.4.0
	gvisor.dev/gvisor v0.0.0-20210322211820-0db21bb9e382
)
