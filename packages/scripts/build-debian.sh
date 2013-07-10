#!/bin/bash

_DIST=$1

PKG_DIR=$(pwd)

function die()
{
  echo $*
  exit 1
}

[[ ! -z "$_DIST" ]] || die "Usage: $0 <dist-tarball>"
[[ -f "$_DIST" ]] || die "Can't find dist package [$_DIST]."
[[ -d debian ]] || die "Run me from the top-level packaging directory."
[[ "$_DIST" == "${_DIST%%.tar.gz}.tar.gz" ]] || die "Tarball name needs to end in .tar.gz."

CL_FILE="$(basename $_DIST)"
CL_FILE="${CL_FILE%%-*}"
CL_FILE="changelog.$CL_FILE"

[[ -f "debian/$CL_FILE" ]] || die "Expected to find changelog file [debian/$CL_FILE]."

pushd $(dirname $_DIST) >/dev/null || die "Can't enter dist dir."
_DIST=$(pwd)/$(basename $_DIST)
popd >/dev/null

mkdir -p output || die "Can't create output dir."

rm -rf build/deb-build
mkdir -p build/deb-build || die "Can't create build dir."
cd build/deb-build || die "Can't enter build dir."

TAR_NAME=$(basename $_DIST)
TAR_NAME=${TAR_NAME%%.tar.gz}
cp $_DIST ${TAR_NAME/-/_}.orig.tar.gz || die "Failed to copy tarball."

tar xfz $_DIST || die "Failed to unpack tarball."
cd * || die "Failed to enter source dir."

cp -r $PKG_DIR/debian . || die "Can't copy debian files."
cd debian || die "Can't enter debian"

rm -f changelog.* || die "Can't remove changelogs."
cp $PKG_DIR/debian/changelog.$CL_FILE changelog || die "Can't copy build-specific changelog."

CL_APP="$(head -n 1 changelog | sed -e 's/ (.*//')"
CL_FULL_VERSION="$(head -n 1 changelog | sed -e 's/.* (//' -e 's/).*//')"

for F in *.in; do
  sed \
    -e "s/__PACKAGE_NAME__/$CL_APP/g" \
    -e "s/__PACKAGE_VERSION__/$CL_FULL_VERSION/g" \
    < $F \
    > ${F%%.in}
done

find . -type d -name .svn | xargs rm -rf

debuild || die "debuild failed."
debuild -S -sa || die "debuild (source) failed."

cd $PKG_DIR

mv build/deb-build/*.tar.gz output/
mv build/deb-build/*.dsc output/
mv build/deb-build/*.build output/
mv build/deb-build/*.changes output/
mv build/deb-build/*.deb output/ || die "Can't move .deb files."

rm -rf build/deb-build
rmdir build 2>/dev/null
