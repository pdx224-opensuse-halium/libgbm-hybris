# RPM packaging

This fork targets openSUSE, so it carries an RPM spec. `debian/` was removed:
nothing in the build path ever invoked it.

```sh
./rpm/build.sh              # -> out/*.rpm
```

Needs `android-headers` and `libhybris-devel` installed first (both are our own
packages, from `rpms/`). `build.sh` checks for them and fails early with a clear
message rather than letting cmake die on a missing gralloc symbol.

## The install path is deliberately Debian-style

```
/usr/lib/aarch64-linux-gnu/gbm/hybris_gbm.so
```

**Not** `/usr/lib64`. Mesa's GBM backend search path is compiled in, and on this
port it is the multiarch directory. If the backend is moved to `/usr/lib64`,
`GBM_BACKEND=hybris` silently finds nothing and falls back — which presents as a
rendering or driver bug, not as a missing file. The path is hardcoded in
`CMakeLists.txt`; the spec's `%files` matches it on purpose.

Note this is separate from the *link* path, which openSUSE does need as
`/usr/lib64` — that is what the "cmake: link against /usr/lib64" commit fixed.
Link path and install path differ here, which is confusing but correct.

## What changed from the old packaging

Previously built by a generic `build_rpm()` helper in
`_work/build-gfx-components.sh`, shared by several graphics components. It
tarred up a `DESTDIR` and declared `%files /usr/`, i.e. it shipped whatever
happened to be in the staging directory rather than a declared list of files.
The spec here names the one file the package provides.

## Licence — unresolved, needs your attention

The old helper hardcoded `License: MIT` for every component it packaged. That
was never verified for this project, and:

- this repository has no `LICENSE` or `COPYING` file;
- **upstream `Linux-on-droid/libgbm-hybris` has none either**, so there is
  nothing to inherit;
- the sources carry no SPDX headers.

The spec keeps `License: MIT` so the package metadata does not change silently,
but it is an assertion nobody has checked. Publishing code with no stated
licence leaves others unable to use it legally, so this should be settled with
upstream before this repo goes public.
