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
[[ -d ubuntu ]] || die "Run me from the top-level packaging directory."
[[ "$_DIST" == "${_DIST%%.tar.gz}.tar.gz" ]] || die "Tarball name needs to end in .tar.gz."

pushd $(dirname $_DIST) >/dev/null || die "Can't enter dist dir."
_DIST=$(pwd)/$(basename $_DIST)
popd >/dev/null

mkdir -p output || die "Can't create output dir."

rm -rf build/ub-build
mkdir -p build/ub-build || die "Can't create build dir."
cd build/ub-build || die "Can't enter build dir."

TAR_NAME=$(basename $_DIST)
TAR_NAME=${TAR_NAME%%.tar.gz}
cp $_DIST ${TAR_NAME/-/_}.orig.tar.gz || die "Failed to copy tarball."

tar xfz $_DIST || die "Failed to unpack tarball."
cd * || die "Failed to enter source dir."

cp -r $PKG_DIR/debian . || die "Can't copy debian files."
cd debian || die "Can't enter debian"

find . -type d -name .svn | xargs rm -rf

rm control || die "Can't remove debian control file."
cp $PKG_DIR/ubuntu/control . || die "Can't copy Ubuntu control file."

mv changelog changelog.debian || die "Can't rename debian changelog."

cp $PKG_DIR/ubuntu/changelog . || die "Can't copy Ubuntu changelog."

echo >> changelog
cat changelog.debian >> changelog || die "Can't update changelog."
rm changelog.debian || die "Can't remove debian changelog."

echo >> changelog
cat ../ChangeLog >> changelog || die "Can't update changelog."

debuild || die "debuild failed."

cd $PKG_DIR

mv build/ub-build/*.tar.gz output/
mv build/ub-build/*.dsc output/
mv build/ub-build/*.build output/
mv build/ub-build/*.changes output/
mv build/ub-build/*.deb output/ || die "Can't move .deb files."

rm -rf build/ub-build
rmdir build 2>/dev/null
