#!/bin/bash

_TRUNK_DIR=$1
shift

PKG_NAME=gcsfs
PKG_NAME_U="$(echo $PKG_NAME | tr '[:lower:]' '[:upper:]')"
PKG_DIR=$(pwd)

function die()
{
  echo $*
  exit 1
}

[[ -d scripts ]] || die "Run me from the top-level packaging directory."
[[ ! -z "$_TRUNK_DIR" ]] || die "Usage: $0 <trunk-dir>"
[[ -f "$_TRUNK_DIR/ChangeLog" ]] || die "Can't find ChangeLog in [$_TRUNK_DIR]."

pushd "$_TRUNK_DIR" >/dev/null || die "Can't change into trunk dir."
_TRUNK_DIR="$(pwd)"
popd >/dev/null

echo "using trunk dir [$_TRUNK_DIR]"

mkdir -p output || die "Can't create output dir."

rm -rf build/gcs-gen 2>/dev/null

mkdir -p build/gcs-gen || die "Can't create build dir."
cd build/gcs-gen || die "Can't enter build dir."

echo -n "copying trunk... "

cp -r $_TRUNK_DIR trunk/ || die "failed to copy"
cd trunk || die "failed to enter dir"

echo "done"
echo -n "cleaning... "

./clean.sh

echo "done"

CL_DATE="$(date '+%a, %d %b %Y %H:%M:%S %z')"

echo "updating changelog"

mv ChangeLog ChangeLog.orig

head -n 1 ChangeLog.orig | sed -e "s/^s3fuse /$PKG_NAME /" > ChangeLog
echo -e -n "\n  * Repackaging for $PKG_NAME\n\n" >> ChangeLog
echo -e -n " -- Tarick Bedeir <tarick@bedeir.com>  $CL_DATE\n\n" >> ChangeLog 

cat ChangeLog.orig >> ChangeLog
rm -f ChangeLog.orig

echo -n "renaming... "

for F in \
  INSTALL \
  README \
  src/base/config.inc \
  src/base/s3fuse.conf.sh \
  man/s3fuse* \
  $(find . -name \*.am);
do
  mv $F $F.orig
  sed \
    -e "s/s3fuse/$PKG_NAME/g" \
    -e "s/S3FUSE/$PKG_NAME_U/g" \
    < $F.orig > $F
  chmod $(stat -f %p $F.orig) $F
  rm -f $F.orig
done

mv src/base/s3fuse.conf.sh src/base/$PKG_NAME.conf.sh || die "can't rename config generator"

for F in man/s3fuse*; do
  mv $F ${F/s3fuse/$PKG_NAME}
done

echo "done"

echo "configuring..."

./build.sh $* >/dev/null || die "Can't configure."

echo "done"

echo -n "making tarball... "

make dist >/dev/null || die "failed"

echo "done"

mv *.tar.gz $PKG_DIR/output/

cd $PKG_DIR || die "Can't return to packaging dir."
rm -rf build/gcs-gen
rmdir build 2>/dev/null

echo "tarball(s) are in [$PKG_DIR/output/]"
