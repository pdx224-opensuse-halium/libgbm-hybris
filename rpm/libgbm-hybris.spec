%global debug_package %{nil}

Name:           libgbm-hybris
Version:        1.0
Release:        0.pdx224
Summary:        GBM backend implemented over the Android gralloc HAL

# NOTE ON THIS TAG: it is inherited, not verified. The previous packaging (a
# generic build_rpm() helper in _work/build-gfx-components.sh) hardcoded
# "License: MIT" for every graphics component it packaged, regardless of what
# each one actually was. Neither this repository nor upstream
# (Linux-on-droid/libgbm-hybris) ships a LICENSE file, and the sources carry no
# SPDX headers, so there is nothing to inherit from. The tag is kept as-is so
# the package metadata does not silently change; it still needs resolving with
# upstream before this is published. See rpm/README.md.
License:        MIT
URL:            https://github.com/Linux-on-droid/libgbm-hybris
Source0:        libgbm-hybris.tar.gz

BuildRequires:  gcc make cmake pkgconfig
BuildRequires:  libdrm-devel
# hybris_gralloc_* comes from libhybris; the Android headers are staged at
# /usr/include/android by the android-headers package (build-time only).
BuildRequires:  libhybris-devel
BuildRequires:  android-headers
Requires:       libhybris

%description
A GBM backend that allocates through the Android gralloc HAL
(hybris_gralloc_*) instead of issuing DRM ioctls to a real KMS device. It is the
foreign GBM device that zink and freedreno import buffer objects from on this
port, selected at runtime with GBM_BACKEND=hybris.

%prep
%setup -q -n libgbm-hybris

%build
# Plain cmake, not %%cmake: CMakeLists.txt hardcodes the install destination
# (see %%files) and the openSUSE macro's prefix handling would fight it.
cmake -DCMAKE_BUILD_TYPE=Release .
%make_build

%install
%make_install

%files
# NOT %%{_libdir}. The backend must land on Mesa's compiled-in GBM backend
# search path, which is the Debian-style multiarch directory even on openSUSE --
# moving it to /usr/lib64 makes GBM_BACKEND=hybris silently fail to load, and
# the fallback looks like a driver bug rather than a missing file.
#
# The directory is co-owned with mesa-pdx224 (which ships dri_gbm.so beside us).
# Co-ownership is legal in RPM and is the right thing here: it means this package
# does not depend on mesa being installed first for the directory to exist.
%dir /usr/lib/aarch64-linux-gnu/gbm
/usr/lib/aarch64-linux-gnu/gbm/hybris_gbm.so

%changelog
* Sun Aug 31 2026 Krzysztof Kolendowicz <koloses@gmail.com> - 1.0-0.pdx224
- Package from an in-tree spec instead of the generic build_rpm() helper, which
  tarred up a DESTDIR and shipped it as "%files /usr/" -- that packaged whatever
  happened to be in the staging directory rather than a declared file list.
