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
[[ -d osx ]] || die "Run me from the top-level packaging directory."

pushd $(dirname "$_DIST") >/dev/null || die "Can't change into trunk dir."
_DIST="$(pwd)/$(basename $_DIST)"
popd >/dev/null

mkdir -p output || die "Can't create output dir."

rm -rf build/osx-build 2>/dev/null

mkdir -p build/osx-build || die "Can't create build dir."
cd build/osx-build || die "Can't enter build dir."

tar xfz $_DIST || die "Can't unpack tarball."
cd * || die "Can't enter unpacked dir."

PACKAGE_NAME=$(grep "^PACKAGE_NAME=" configure | sed -e "s/^PACKAGE_NAME='//" -e "s/'\$//")
PACKAGE_VERSION=$(grep "^PACKAGE_VERSION=" configure | sed -e "s/^PACKAGE_VERSION='//" -e "s/'\$//")

BUILD_DIR=$(pwd)

./configure --enable-darwin --enable-osx-bundle || die "configure failed."
make || die "make failed."

cd man || die "Can't enter man dir."
make pdfs || die "make pdfs failed."

cd $PKG_DIR/build/osx-build || die "Can't enter osx-build."

mkdir -p $PACKAGE_NAME.app/Contents || die "Can't create app bundle dir."
cd $PACKAGE_NAME.app/Contents || die "Can't enter app bundle dir."

mkdir -p MacOS Resources/Scripts Resources/libs Resources/bin || die "Can't create content dirs."

sed -e "s/__PACKAGE_VERSION__/$PACKAGE_VERSION/g" < $PKG_DIR/osx/Info.plist.in > Info.plist || die "Can't generate plist."

cp $PKG_DIR/osx/applet MacOS || die "Can't copy applet."
cp $BUILD_DIR/COPYING $BUILD_DIR/ChangeLog $BUILD_DIR/README Resources || die "Can't copy documents."
cp $BUILD_DIR/src/base/$PACKAGE_NAME.conf Resources || die "Can't copy config file."
cp $PKG_DIR/osx/applet.icns $PKG_DIR/osx/applet.rsrc Resources || die "Can't copy applet resources."
cp $PKG_DIR/osx/main.scpt Resources/Scripts || die "Can't copy script."

cd $BUILD_DIR || die "Can't enter build dir."

for F in $(grep bin_PROGRAMS src/Makefile.am | sed -e 's/^.*=//'); do
  cp src/$F $PKG_DIR/build/osx-build/$PACKAGE_NAME.app/Contents/Resources/bin || die "Can't copy binary [$F]."
done

cd $PKG_DIR/build/osx-build || die "Can't enter osx-build."

for F in $PACKAGE_NAME.app/Contents/Resources/bin/*; do
  dylibbundler -od -b -x $F -d $PACKAGE_NAME.app/Contents/Resources/libs
done

cd $PKG_DIR || die "Can't enter packaging dir."

mkdir build/osx-build/manual || die "Can't create manual dir."
cp $BUILD_DIR/man/*.pdf build/osx-build/manual || die "Can't copy manual PDFs."
cp $BUILD_DIR/COPYING $BUILD_DIR/ChangeLog $BUILD_DIR/README build/osx-build || die "Can't copy documents to image root."
rm -rf $BUILD_DIR || die "Can't remove temporary build dir."

hdiutil create \
  -fs HFSX -layout SPUD -format UDZO -scrub -uid 99 -gid 99 -ov \
  -volname "$PACKAGE_NAME $PACKAGE_VERSION" \
  "$PACKAGE_NAME-$PACKAGE_VERSION.dmg" \
  -srcfolder build/osx-build

mv $PACKAGE_NAME-$PACKAGE_VERSION.dmg output/ || die "Can't move output image."

rm -rf build/osx-build
rmdir build 2>/dev/null
