## 0.2.6
* Implement `Copy` for `FontInfo`.
* Implement `PartialEq`, `Eq`, `Hash` for `Vertex`, `VertexType`, `Rect`, `HMetrics`, `VMetrics`.
* Require byteorder `1.1` as a minimum to avoid compile errors with earlier versions.

## 0.2.5
* Fix `get_glyph_shape` panic with Consolas character `\u{feff}`.

## 0.2.4
* Remove all unsafe usage.
* Fix glyph positioning bug for compound glyphs (#18).
* Optimise compound glyph shape computation.

## 0.2.3
* Add `is_collection(&[u8]) -> bool`.
* Remove most unsafe usages.
* `VertexType` implements `Eq`.
* Optimise API performance using new benchmark/regression suite

```
name                                        control ns/iter  change ns/iter  diff ns/iter   diff %  speedup
find_glyph_index_deja_vu_mono               1,189            856                     -333  -28.01%   x 1.39
get_glyph_bitmap_box_subpixel_deja_vu_mono  859              696                     -163  -18.98%   x 1.23
get_glyph_box_deja_vu_mono                  617              276                     -341  -55.27%   x 2.24
get_glyph_h_metrics_deja_vu_mono            204              184                      -20   -9.80%   x 1.11
get_glyph_shape_deja_vu_mono                12,304           12,950                   646    5.25%   x 0.95
get_v_metrics_deja_vu_mono                  360              100                     -260  -72.22%   x 3.60
scale_for_pixel_height_deja_vu_mono         145              118                      -27  -18.62%   x 1.23
```

## 0.2.2
* Merge a number of bugfixes, update documentation links, add new debugging features.

## 0.2.1
* Fix `attempt to subtract with overflow` error in get_glyph_kern_advance.

## 0.2
* `FontInfo` is now generic in the storage for the font data, allowing flexible management of font data lifetimes. This is a breaking change.

## 0.1.2
* Fix for edge case behaviour for `get_glyph_pair_kern_advance` by switching to `i32` instead of `u32` to match stb_truetype.h (see issue #3).

## 0.1.1
* Fix for glyf table format 12 and 13 handling to match implementation in stb_truetype.h (see issue #2).

## 0.1
* Initial release.
