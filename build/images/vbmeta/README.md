# VBMETA data for another partition

The `vbmeta()` template in `//build/images/vbmeta.gni` allows for the addition
of extra, arbitrary, hash descriptors in the vbmeta image that's generated for
a zbi.  The need for these are product-specific.

The format of these are:

```json
{
    "type": "partition",
    "name": "partition_name",
    "size": "104448",
    "flags": "1",
    "min_avb_version": "1.1"
}
```

- `name` = (string) The name of the partition as it to appear in the vbmeta image
- `size` = (int as string) The size of the partition in bytes
- `flags` = (int) The bit-wise set of flag values
- `min_avb_version` = (version x.y as a string) the minimum avb version that the
   resultant vbmeta image requires if it includes this descriptor.

Note: These files cannot contain any comments, and must strictly conform to the
[JSON](http://json.org) spec.
