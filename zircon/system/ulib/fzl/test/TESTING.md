# Running FZL Tests

1. `fx set workstation.x64 --with-base //garnet/packages/tests:zircon`
2. `fx build`
3. `fx serve`
4. In another terminal: `fx run -k -N`
5. For the zxtests: at the command prompt: `system/test/sys/fzl-zxtest-tests`
6. For the unittests: at the command prompt: `system/test/sys/fzl-tests`
