module go.fuchsia.dev/fuchsia/tools

go 1.15

require (
	cloud.google.com/go v0.64.0
	github.com/creack/pty v1.1.11
	github.com/go-yaml/yaml v2.1.0+incompatible
	github.com/golang/glog v0.0.0-20160126235308-23def4e6c14b
	github.com/golang/protobuf v1.4.2
	github.com/google/go-cmp v0.5.1
	github.com/google/subcommands v1.2.0
	github.com/julienschmidt/httprouter v1.3.0 // indirect
	github.com/pkg/sftp v1.11.0
	go.chromium.org/luci v0.0.0-20200821095028-79bd0b964db3
	go.fuchsia.dev/fuchsia/garnet v0.0.0-20200821151753-3226fa91b98e
	go.fuchsia.dev/fuchsia/src v0.0.0-20200821151753-3226fa91b98e
	golang.org/x/crypto v0.0.0-20200820211705-5c72a883971a
	golang.org/x/net v0.0.0-20200813134508-3edf25e44fcc
	golang.org/x/sync v0.0.0-20200625203802-6e8e738ad208
	golang.org/x/sys v0.0.0-20200821140526-fda516888d29
	golang.org/x/tools v0.0.0-20200821151209-74543c403428 // indirect
	google.golang.org/protobuf v1.25.0 // indirect
	honnef.co/go/tools v0.0.1-2020.1.5 // indirect
)

replace (
	cloud.google.com/go => ../third_party/golibs/github.com/googleapis/google-cloud-go
	github.com/creack/pty => ../third_party/golibs/github.com/creack/pty
	github.com/go-yaml/yaml => ../third_party/golibs/github.com/go-yaml/yaml
	github.com/golang/glog => ../third_party/golibs/github.com/golang/glog
	github.com/golang/protobuf => ../third_party/golibs/github.com/golang/protobuf
	github.com/google/go-cmp => ../third_party/golibs/github.com/google/go-cmp
	github.com/google/subcommands => ../third_party/golibs/github.com/google/subcommands
	github.com/pkg/sftp => ../third_party/golibs/github.com/pkg/sftp
	go.fuchsia.dev/fuchsia/garnet => ../garnet
	go.fuchsia.dev/fuchsia/src => ../src
	golang.org/x/crypto => ../third_party/golibs/golang.org/x/crypto
	golang.org/x/net => ../third_party/golibs/golang.org/x/net
	golang.org/x/sync => ../third_party/golibs/golang.org/x/sync
	golang.org/x/sys => ../third_party/golibs/golang.org/x/sys
)
