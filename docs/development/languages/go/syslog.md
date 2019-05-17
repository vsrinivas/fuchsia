# Syslog

This document explains how to use the golang syslog library.


## gn dependency

```
deps = [
    "//garnet/public/lib/component/go/src/app",
    "//garnet/public/lib/syslog/go/src/syslog",
]
```

### Initialization

```golang
import (
    "app/context"
    "syslog"
)

func main() {
  ctx := context.CreateFromStartupInfo()
  {
    // Global tags, max 4 tags can be passed. Every log message would be tagged using these.
    l, err := syslog.NewLoggerWithDefaults(ctx.Connector(), tag1, tag2)
    if err != nil {
      panic(err)
    }
    syslog.SetDefaultLogger(l)
  }
}
```

### Log messages

```golang
syslog.Infof("my msg: %d", 10);

// Allow message specific tagging. This message is going to be tagged with
// this local tag and any global tag passed during initialization.
syslog.InfoTf("tag", "my msg: %d", 10);

syslog.Warnf("my msg: %d", 10);
syslog.WarnTf("tag", "my msg: %d", 10);

syslog.Errorf("my msg: %d", 10);
syslog.ErrorTf("tag", "my msg: %d", 10);

syslog.Fatalf("my msg: %d", 10);
syslog.FatalTf("tag", "my msg: %d", 10);

syslog.VLogf(1, "my msg: %d", 10); // verbose logs
syslog.VLogTf(1, "tag", "my msg: %d", 10); // verbose logs
```

### Reference
[Golang APIs](/garnet/public/lib/syslog/go/src/syslog/logger.go)
