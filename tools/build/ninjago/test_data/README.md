# Ninja Test Data

Acquired from building gn, since Fuchsia's build graph is way too big.

gn at 501b49a3ab4f0d099457b6e5b62c709a1d2311be

```bash
git clone https://gn.googlesource.com/gn
cd gn
git checkout 501b49a3ab4f0d099457b6e5b62c709a1d2311be
python build/gen.py
ninja -C out gn
cp out/.ninja_log ninja_lot
ninja -C out -t compdb > compdb.json
ninja -C out -t graph gn > graph.dot
```
