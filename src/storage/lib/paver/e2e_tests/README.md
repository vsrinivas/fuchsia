The pave tests check that an upgrade build can be paved using Zedboot from the
downgrade build. This can be done by running:

```
% $(fx get-build-dir)/host_x64/e2e_tests_pave \
  --ssh-private-key ~/.ssh/fuchsia_ed25519 \
  --downgrade-builder-name fuchsia/global.ci/fuchsia-x64-release-build_only \
  --upgrade-builder-name --upgrade-fuchsia-build-dir $(fx get-build-dir) \
  -device-hostname <IP-ADDRESS>
```
