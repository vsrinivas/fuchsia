# Ninja Test Data

Test data is from a Zircon build.

Fuchsia at c365fdad3a1fec2877d95d13715ed423da73c55f

```bash
cd fuchsia
git checkout c365fdad3a1fec2877d95d13715ed423da73c55f
fx set core.x64
fx clean-build
cp out/default.zircon/.ninja_log ninja_log
ninja -C out/default.zircon -t compdb > compdb.json
ninja -C out/default.zircon -t graph gn > graph.dot
gzip ninja_log compdb.json graph.dot
```
