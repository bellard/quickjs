#!/bin/sh
# Release the QuickJS source code

set -e

version=`cat VERSION`

if [ "$1" = "-h" ] ; then
    echo "release.sh [release_list]"
    echo ""
    echo "release_list: extras binary win_binary cosmo_binary quickjs"

    exit 1
fi

release_list="extras binary win_binary cosmo_binary quickjs"

if [ "$1" != "" ] ; then
    release_list="$1"
fi

#################################################"
# extras

if echo $release_list | grep -w -q extras ; then

d="quickjs-${version}"
name="quickjs-extras-${version}"
outdir="/tmp/${d}"

rm -rf $outdir
mkdir -p $outdir $outdir/unicode $outdir/tests

cp unicode/* $outdir/unicode
cp -a tests/bench-v8 $outdir/tests

( cd /tmp && tar Jcvf /tmp/${name}.tar.xz ${d} )

fi

#################################################"
# Windows binary release

if echo $release_list | grep -w -q win_binary ; then

# win64

dlldir=/usr/x86_64-w64-mingw32/sys-root/mingw/bin
cross_prefix="x86_64-w64-mingw32-"
d="quickjs-win-x86_64-${version}"
outdir="/tmp/${d}"

rm -rf $outdir
mkdir -p $outdir

make clean
make CONFIG_WIN32=y clean

make CONFIG_WIN32=y CONFIG_LTO=y qjs.exe
cp qjs.exe $outdir
${cross_prefix}strip $outdir/qjs.exe
cp $dlldir/libwinpthread-1.dll $outdir

( cd /tmp/$d && rm -f ../${d}.zip && zip -r ../${d}.zip . )

make CONFIG_WIN32=y clean

# win32

dlldir=/usr/i686-w64-mingw32/sys-root/mingw/bin
cross_prefix="i686-w64-mingw32-"
d="quickjs-win-i686-${version}"
outdir="/tmp/${d}"

rm -rf $outdir
mkdir -p $outdir

make clean
make CONFIG_WIN32=y clean

make CONFIG_WIN32=y CONFIG_M32=y CONFIG_LTO=y qjs.exe
cp qjs.exe $outdir
${cross_prefix}strip $outdir/qjs.exe
cp $dlldir/libwinpthread-1.dll $outdir

( cd /tmp/$d && rm -f ../${d}.zip && zip -r ../${d}.zip . )

fi

#################################################"
# Linux binary release

if echo $release_list | grep -w -q binary ; then

make clean
make CONFIG_WIN32=y clean
make -j4 CONFIG_LTO=y qjs run-test262
strip qjs run-test262

d="quickjs-linux-x86_64-${version}"
outdir="/tmp/${d}"

rm -rf $outdir
mkdir -p $outdir

cp qjs run-test262 $outdir

( cd /tmp/$d && rm -f ../${d}.zip && zip -r ../${d}.zip . )

make clean
make -j4 CONFIG_LTO=y CONFIG_M32=y qjs run-test262
strip qjs run-test262

d="quickjs-linux-i686-${version}"
outdir="/tmp/${d}"

rm -rf $outdir
mkdir -p $outdir

cp qjs run-test262 $outdir

( cd /tmp/$d && rm -f ../${d}.zip && zip -r ../${d}.zip . )

fi

#################################################"
# Cosmopolitan binary release

if echo $release_list | grep -w -q cosmo_binary ; then

export PATH=$PATH:$HOME/cosmocc/bin

d="quickjs-cosmo-${version}"
outdir="/tmp/${d}"

rm -rf $outdir
mkdir -p $outdir

make clean
make CONFIG_COSMO=y -j4 qjs run-test262
cp qjs run-test262 $outdir
cp readme-cosmo.txt $outdir/readme.txt

( cd /tmp/$d && rm -f ../${d}.zip && zip -r ../${d}.zip . )

fi

#################################################"
# quickjs

if echo $release_list | grep -w -q quickjs ; then

make build_doc

d="quickjs-${version}"
outdir="/tmp/${d}"

rm -rf $outdir
mkdir -p $outdir $outdir/doc $outdir/tests $outdir/examples

cp Makefile VERSION TODO Changelog readme.txt LICENSE \
   release.sh unicode_download.sh \
   qjs.c qjsc.c repl.js \
   quickjs.c quickjs.h quickjs-atom.h \
   quickjs-libc.c quickjs-libc.h quickjs-opcode.h \
   cutils.c cutils.h list.h \
   libregexp.c libregexp.h libregexp-opcode.h \
   libunicode.c libunicode.h libunicode-table.h \
   dtoa.c dtoa.h \
   unicode_gen.c unicode_gen_def.h \
   run-test262.c test262o.conf test262.conf \
   test262o_errors.txt test262_errors.txt \
   $outdir

cp tests/*.js tests/*.patch tests/bjson.c $outdir/tests

cp examples/*.js examples/*.c examples/*.json $outdir/examples

cp doc/quickjs.texi doc/quickjs.pdf doc/quickjs.html \
   $outdir/doc

( cd /tmp && tar Jcvf /tmp/${d}.tar.xz ${d} )

fi
