#!/bin/sh
# Release the QuickJS source code

set -e

version=`cat VERSION`

if [ "$1" = "-h" ] ; then
    echo "release.sh [all]"
    echo ""
    echo "all: build all the archives. Otherwise only build the quickjs source archive."
    exit 1
fi

extras="no"
binary="no"
quickjs="no"

if [ "$1" = "all" ] ; then
    extras="yes"
    binary="yes"
    quickjs="yes"
elif [ "$1" = "binary" ] ; then
    binary="yes"
else
    quickjs="yes"
fi

#################################################"
# extras

if [ "$extras" = "yes" ] ; then

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
# binary release

if [ "$binary" = "yes" ] ; then

d="quickjs-linux-x86_64-${version}"
name="quickjs-linux-x86_64-${version}"
outdir="/tmp/${d}"

rm -rf $outdir
mkdir -p $outdir

files="qjs run-test262"

make -j4 $files

strip $files
cp $files $outdir

( cd /tmp/$d && rm -f ../${name}.zip && zip -r ../${name}.zip . )

fi

#################################################"
# quickjs

if [ "$quickjs" = "yes" ] ; then

make build_doc

d="quickjs-${version}"
outdir="/tmp/${d}"

rm -rf $outdir
mkdir -p $outdir $outdir/doc $outdir/tests $outdir/examples

cp Makefile VERSION TODO Changelog readme.txt release.sh unicode_download.sh \
   qjs.c qjsc.c qjscalc.js repl.js \
   quickjs.c quickjs.h quickjs-atom.h \
   quickjs-libc.c quickjs-libc.h quickjs-opcode.h \
   cutils.c cutils.h list.h \
   libregexp.c libregexp.h libregexp-opcode.h \
   libunicode.c libunicode.h libunicode-table.h \
   libbf.c libbf.h \
   jscompress.c unicode_gen.c unicode_gen_def.h \
   run-test262.c test262o.conf test262.conf \
   test262o_errors.txt test262_errors.txt \
   $outdir

cp tests/*.js tests/*.patch tests/bjson.c $outdir/tests

cp examples/*.js examples/*.c $outdir/examples

cp doc/quickjs.texi doc/quickjs.pdf doc/quickjs.html \
   doc/jsbignum.texi doc/jsbignum.html doc/jsbignum.pdf \
   $outdir/doc 

( cd /tmp && tar Jcvf /tmp/${d}.tar.xz ${d} )

fi
