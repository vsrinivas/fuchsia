# Testing toolchain.{h,cc} (manually)
Compile `analytics_cpp_core_dev_tools_toolchain_manualtest`
```
fx set core.x64  --with-host '//src/lib/analytics/cpp/core_dev_tools:tests'
fx build host_x64/analytics_cpp_core_dev_tools_toolchain_manualtest
```

## Test in-tree
Run `analytics_cpp_core_dev_tools_toolchain_manualtest`
```
out/default/host_x64/analytics_cpp_core_dev_tools_toolchain_manualtest
```

Expected result:
```
Toolchain: in-tree
Version: <latest commit date>
```

## Test in SDK
Copy `out/default/host_x64/analytics_cpp_core_dev_tools_toolchain_manualtest` to `tools/` folder of
the SDK and run it there.

Expected result:
```
Toolchain: sdk
Version: <build id>
```
