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

rm -rf build/rpm-build 2>/dev/null

mkdir -p build/rpm-build || die "Can't create build dir."
cd build/rpm-build || die "Can't enter build dir."

mkdir RPMS SOURCES SPECS BUILD SRPMS || die "Can't create repo dirs."

cd $PKG_DIR

cp dist/*.spec build/rpm-build/SPECS || die "Can't copy spec file."
cp $_DIST build/rpm-build/SOURCES || die "Can't copy source package."

rpmbuild --define "_topdir $PKG_DIR/build/rpm-build" -v -v -v -v -v -ba build/rpm-build/SPECS/*.spec || die "rpmbuild failed."

mv build/rpm-build/SRPMS/* . || die "No source RPMs to copy."
mv build/rpm-build/RPMS/*/* . || die "No RPMs to copy."

rm -rf build/rpm-build
