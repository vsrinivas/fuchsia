# Storage integration test

The integration test for storage capabilities is comprised of three components:

```
     storage_realm
     /           \
 memfs          storage_user
```

The `memfs` and `storage_user` components are pretty simple. `memfs` runs memfs,
a mutable in-memory filesystem, and makes it available on its outgoing directory
at the path `/minfs`. `storage_user` will forward any open connections to
`/data` on its outgoing directory to `/data` in its namespace.

The manifest for `storage_realm` connect `memfs` and `storage_user` together,
creating storage capabilities from `memfs`'s exposed directory and offering them
to `storage_user`. This allows `storage_realm` to both access the memfs instance
directly, and to access it through `storage_user`'s exposed `/data` directory.

With this set up, the `storage_realm` component binds to `storage_user` and
writes a file to the `/data` directory, and then binds to `memfs` and attempts
to read the file back from the sub-directory that should have been generated for
the storage capability used by `storage_user`.
