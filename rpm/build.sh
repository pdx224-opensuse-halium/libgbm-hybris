#!/bin/bash
# Build the libgbm-hybris RPM on openSUSE (aarch64).
#
#   ./rpm/build.sh [output-dir]
#
# The source tarball is generated from THIS git tree with `git archive`, so the
# RPM always corresponds to the commit it was built from.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/.." && pwd)"
OUT="${1:-$SRC/out}"
TOP="${RPM_TOPDIR:-$HOME/rpmbuild}"

echo "=== build deps ==="
zypper --non-interactive install -y \
  gcc gcc-c++ make cmake pkgconf-pkg-config libdrm-devel 2>&1 | tail -3

# libhybris-devel + android-headers are our own packages; they must already be
# installed (they come from rpms/). Fail early and clearly rather than letting
# cmake report a missing gralloc symbol later.
for f in /usr/include/android /usr/include/hybris; do
  [ -d "$f" ] || { echo "ERROR: $f missing -- install android-headers and libhybris-devel first" >&2; exit 1; }
done

echo "=== stage sources from git ==="
mkdir -p "$TOP"/{SOURCES,SPECS,RPMS,BUILD,BUILDROOT}
git -C "$SRC" archive --format=tar --prefix=libgbm-hybris/ HEAD \
  | gzip > "$TOP/SOURCES/libgbm-hybris.tar.gz"
cp "$HERE/libgbm-hybris.spec" "$TOP/SPECS/"

echo "=== rpmbuild ==="
rpmbuild -bb --define "_topdir $TOP" "$TOP/SPECS/libgbm-hybris.spec"

mkdir -p "$OUT"
cp "$TOP"/RPMS/aarch64/libgbm-hybris-*.rpm "$OUT"/ 2>/dev/null || true
echo "=== done ==="
ls -la "$OUT"/libgbm-hybris-*.rpm
