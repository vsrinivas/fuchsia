# Testing system_info.{h,cc} (manually)
Compile `analytics_cpp_core_dev_tools_system_info_manualtest`
```
fx set core.x64  --with-host '//src/lib/analytics/cpp/core_dev_tools:tests'
fx build host_x64/analytics_cpp_core_dev_tools_system_info_manualtest
```

Run `analytics_cpp_core_dev_tools_toolchain_manualtest`
```
out/default/host_x64/analytics_cpp_core_dev_tools_system_info_manualtest
```

Expected result:
Same as the output of `uname -ms`

