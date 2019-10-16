echo '' > fdio-all.h
for f in $ZIRCON_BUILD_DIR/sysroot/include/fdio/*.h; do
  echo "#include <$f>" >> fdio-all.h
done
bindgen -l  -o fdio-all.rs fdio-all.h \
  --whitelist-function "(__)?(zx|fd)(io|rio|sio)_.+" \
  --whitelist-type "(__)?(zx|fd)(io|rio|sio)_.+|v(dir|na).+" \
  --whitelist-var "(__)?(O_.+|(ZX|FD)(IO|RIO|SIO)_.+|VFS_.+|MAX_ZXIO_FD|VNATTR_.+|^ATTR_.+|^V_.+|^VTYPE.+|^DTYPE.+|^WATCH_.+|(zx|fd)(io|rio|sio)_.+|vfs_.+|max_zxio_fd|vnattr_.+|^attr_.+|^v_.+|^vtype.+|^dtype.+|^watch_.+)" \
  -- --sysroot=$ZIRCON_BUILD_DIR/sysroot -I $ZIRCON_BUILD_DIR/sysroot/include
rm fdio-all.h