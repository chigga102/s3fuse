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

#tar -x --no-anchored --strip-components=1 -z -f $_DIST ChangeLog || die "Failed to extract ChangeLog."

cp -r $PKG_DIR/debian . || die "Can't copy debian files."
cd debian || die "Can't enter debian"

find . -type d -name .svn | xargs rm -rf

echo >> changelog
cat ../ChangeLog >> changelog || die "Can't update changelog."
#rm ../ChangeLog || die "Can't remove ChangeLog."

debuild || die "debuild failed."

cd $PKG_DIR

mv build/deb-build/*.tar.gz output/
mv build/deb-build/*.dsc output/
mv build/deb-build/*.build output/
mv build/deb-build/*.changes output/
mv build/deb-build/*.deb output/ || die "Can't move .deb files."

rm -rf build/deb-build
rmdir build 2>/dev/null
