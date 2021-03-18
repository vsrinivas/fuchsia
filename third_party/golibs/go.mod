module go.fuchsia.dev/fuchsia

go 1.16

replace (
	cloud.google.com/go => fuchsia.googlesource.com/third_party/github.com/googleapis/google-cloud-go.git v0.0.0-20200623164852-ad4f9324cdd7
	github.com/creack/pty => fuchsia.googlesource.com/third_party/github.com/creack/pty.git v0.0.0-20191209115840-8ab47f72e854
	github.com/dustin/go-humanize => fuchsia.googlesource.com/third_party/go-humanize.git v0.0.0-20180713052910-9f541cc9db5d

	// We forked this one.
	github.com/flynn/go-tuf => fuchsia.googlesource.com/third_party/go-tuf.git v0.0.0-20200826175457-a90d70916791
	github.com/fsnotify/fsnotify => fuchsia.googlesource.com/third_party/github.com/fsnotify/fsnotify.git v0.0.0-20200311173518-45d7d09e39ef
	github.com/golang/protobuf => fuchsia.googlesource.com/third_party/github.com/golang/protobuf.git v0.0.0-20200514204437-d04d7b157bb5
	github.com/google/go-cmp => fuchsia.googlesource.com/third_party/github.com/google/go-cmp.git v0.0.0-20200818193711-d2fcc899bdc2
	github.com/google/shlex => fuchsia.googlesource.com/third_party/github.com/google/shlex.git v0.0.0-20191202100458-e7afc7fbc510
	github.com/google/subcommands => fuchsia.googlesource.com/third_party/github.com/google/subcommands.git v0.0.0-20190904161856-24aea2b9b9c1
	github.com/kr/fs => fuchsia.googlesource.com/third_party/github.com/kr/fs.git v0.0.0-20131111012553-2788f0dbd169
	github.com/kr/pretty => fuchsia.googlesource.com/third_party/github.com/kr/pretty.git v0.0.0-20180506083345-73f6ac0b30a9
	github.com/pkg/errors => fuchsia.googlesource.com/third_party/github.com/pkg/errors.git v0.0.0-20170505043639-c605e284fe17
	github.com/pkg/sftp => fuchsia.googlesource.com/third_party/github.com/pkg/sftp.git v0.0.0-20200111004458-561618205222
	github.com/spf13/pflag => fuchsia.googlesource.com/third_party/github.com/spf13/pflag.git v0.0.0-20201009195203-85dd5c8bc61c
	go.uber.org/atomic => fuchsia.googlesource.com/third_party/go.uber.org/atomic.git v0.0.0-20200515224902-7ccfa79b883f
	go.uber.org/multierr => fuchsia.googlesource.com/third_party/go.uber.org/multierr.git v0.0.0-20200423170120-4a27324ebb71
	golang.org/x/crypto => fuchsia.googlesource.com/third_party/golang.org/x/crypto.git v0.0.0-20200602180216-279210d13fed
	golang.org/x/net => fuchsia.googlesource.com/third_party/golang.org/x/net.git v0.0.0-20190827160401-ba9fcec4b297
	golang.org/x/sync => fuchsia.googlesource.com/third_party/golang.org/x/sync.git v0.0.0-20190911185100-cd5d95a43a6e
	golang.org/x/sys => fuchsia.googlesource.com/third_party/golang.org/x/sys.git v0.0.0-20210303074136-134d130e1a04
	golang.org/x/time => fuchsia.googlesource.com/third_party/golang.org/x/time.git v0.0.0-20190921001708-c4c64cad1fd0
	golang.org/x/xerrors => fuchsia.googlesource.com/third_party/golang.org/x/xerrors.git v0.0.0-20200804184101-5ec99f83aff1
	google.golang.org/grpc => fuchsia.googlesource.com/third_party/github.com/grpc/grpc-go.git v0.0.0-20200423161235-754ee590a4f3
	google.golang.org/protobuf => fuchsia.googlesource.com/third_party/github.com/protocolbuffers/protobuf-go.git v0.0.0-20200622203215-3f7a61f89bb6
	gopkg.in/yaml.v2 => fuchsia.googlesource.com/third_party/github.com/go-yaml/yaml.git v0.0.0-20200121171940-53403b58ad1b
)

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
	github.com/stretchr/testify v1.7.0 // indirect
	github.com/tent/canonical-json-go v0.0.0-20130607151641-96e4ba3a7613 // indirect
	go.uber.org/atomic v1.7.0 // indirect
	go.uber.org/multierr v1.6.0
	golang.org/x/crypto v0.0.0-20210317152858-513c2a44f670
	golang.org/x/mod v0.4.2 // indirect
	golang.org/x/net v0.0.0-20210316092652-d523dce5a7f4
	golang.org/x/sync v0.0.0-20210220032951-036812b2e83c
	golang.org/x/sys v0.0.0-20210317225723-c4fcb01b228e
	golang.org/x/time v0.0.0-20210220033141-f8bda1e9f3ba // indirect
	gonum.org/v1/gonum v0.9.0
	google.golang.org/api v0.42.0 // indirect
	google.golang.org/genproto v0.0.0-20210317182105-75c7a8546eb9 // indirect
	google.golang.org/grpc v1.36.0
	google.golang.org/protobuf v1.26.0
	gopkg.in/check.v1 v1.0.0-20201130134442-10cb98267c6c // indirect
	gopkg.in/yaml.v2 v2.4.0
	gvisor.dev/gvisor v0.0.0-20210317193509-8a3f44a54fe8
)
