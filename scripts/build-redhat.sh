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
[[ -d dist ]] || die "Run me from the top-level packaging directory."

CL_FILE="$(basename $_DIST)"
CL_FILE="${CL_FILE%%-*}"
CL_FILE="changelog.$CL_FILE"

[[ -f "dist/$CL_FILE" ]] || die "Expected to find changelog file [dist/$CL_FILE]."

mkdir -p output || die "Can't create output dir."

rm -rf build/rpm-build 2>/dev/null

mkdir -p build/rpm-build || die "Can't create build dir."
cd build/rpm-build || die "Can't enter build dir."

mkdir RPMS SOURCES SPECS BUILD SRPMS || die "Can't create repo dirs."

cd $PKG_DIR

cp dist/*.spec build/rpm-build/SPECS || die "Can't copy spec file."
cp $_DIST build/rpm-build/SOURCES || die "Can't copy source package."

CL_APP="$(head -n 1 dist/$CL_FILE | sed -e 's/ (.*//')"
CL_FULL_VERSION="$(head -n 1 dist/$CL_FILE | sed -e 's/.* (//' -e 's/).*//')"
CL_VERSION="${CL_FULL_VERSION%%-*}"
CL_RELEASE="${CL_FULL_VERSION#*-}"
CL_RELEASE="${CL_RELEASE//-/_}"
CL_PKG_NAME="${CL_APP}-${CL_FULL_VERSION%-*}"

EXPECTED_TARBALL="$CL_PKG_NAME.tar.gz"

[[ -f "build/rpm-build/SOURCES/$EXPECTED_TARBALL" ]] || die "Expected tarball to be named [$EXPECTED_TARBALL]."

rpmbuild \
  --define "_topdir $PKG_DIR/build/rpm-build" \
  --define "name $CL_APP" \
  --define "version $CL_VERSION" \
  --define "release $CL_RELEASE" \
  --define "pkg_name $CL_PKG_NAME" \
  -ba build/rpm-build/SPECS/*.spec \
  || die "rpmbuild failed."

mv build/rpm-build/SRPMS/* output/. || die "Can't move source RPMs."
mv build/rpm-build/RPMS/*/* output/. || die "Can't move RPMs."

rm -rf build/rpm-build
rmdir build 2>/dev/null
