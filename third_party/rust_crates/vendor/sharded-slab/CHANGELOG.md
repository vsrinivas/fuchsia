<a name="0.0.9"></a>
### 0.0.9 (2020-04-03)


#### Features

* **Config:**  validate concurrent refs ([9b32af58](9b32af58), closes [#21](21))
* **Pool:**
  *  add `fmt::Debug` impl for `Pool` ([ffa5c7a0](ffa5c7a0))
  *  add `Default` impl for `Pool` ([d2399365](d2399365))
  *  add a sharded object pool for reusing heap allocations (#19) ([89734508](89734508), closes [#2](2), [#15](15))
* **Slab::take:**  add exponential backoff when spinning ([6b743a27](6b743a27))

#### Bug Fixes

*   incorrect wrapping when overflowing maximum ref count ([aea693f3](aea693f3), closes [#22](22))



<a name="0.0.8"></a>
### 0.0.8 (2020-01-31)


#### Bug Fixes

*   `remove` not adding slots to free lists ([dfdd7aee](dfdd7aee))



<a name="0.0.7"></a>
### 0.0.7 (2019-12-06)


#### Bug Fixes

* **Config:**  compensate for 0 being a valid TID ([b601f5d9](b601f5d9))
* **DefaultConfig:**
  *  const overflow on 32-bit ([74d42dd1](74d42dd1), closes [#10](10))
  *  wasted bit patterns on 64-bit ([8cf33f66](8cf33f66))



<a name="0.0.6"></a>
## 0.0.6 (2019-11-08)


#### Features

* **Guard:**  expose `key` method #8 ([748bf39b](748bf39b))



<a name="0.0.5"></a>
## 0.0.5 (2019-10-31)


#### Performance

*   consolidate per-slot state into one AtomicUsize (#6) ([f1146d33](f1146d33))

#### Features

*   add Default impl for Slab ([61bb3316](61bb3316))



<a name="0.0.4"></a>
## 0.0.4 (2019-21-30)


#### Features

*   prevent items from being removed while concurrently accessed ([872c81d1](872c81d1))
*   added `Slab::remove` method that marks an item to be removed when the last thread
    accessing it finishes ([872c81d1](872c81d1))

#### Bug Fixes

*   nicer handling of races in remove ([475d9a06](475d9a06))

#### Breaking Changes

*   renamed `Slab::remove` to `Slab::take` ([872c81d1](872c81d1))
*   `Slab::get` now returns a `Guard` type ([872c81d1](872c81d1))


<a name="0.0.3"></a>
## 0.0.3 (2019-07-30)


#### Bug Fixes

*   split local/remote to fix false sharing & potential races ([69f95fb0](69f95fb0))
*   set next pointer _before_ head ([cc7a0bf1](cc7a0bf1))

#### Breaking Changes

*   removed potentially racy `Slab::len` and `Slab::capacity` methods ([27af7d6c](27af7d6c))

<a name="0.0.2"></a>
## 0.0.2 (2019-03-30)


#### Bug Fixes

*   fix compilation failure in release mode ([617031da](617031da))


<a name="0.0.1"></a>
## 0.0.1 (2019-02-30)

- Initial release
