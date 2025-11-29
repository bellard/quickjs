#!/bin/sh
set -e

version="17.0.0"
url="ftp://ftp.unicode.org/Public"

files="CaseFolding.txt DerivedNormalizationProps.txt PropList.txt \
SpecialCasing.txt CompositionExclusions.txt ScriptExtensions.txt \
UnicodeData.txt DerivedCoreProperties.txt NormalizationTest.txt Scripts.txt \
PropertyValueAliases.txt"

mkdir -p unicode

for f in $files; do
    g="${url}/${version}/ucd/${f}"
    wget $g -O unicode/$f
done

wget "${url}/${version}/ucd/emoji/emoji-data.txt" -O unicode/emoji-data.txt

wget "${url}/${version}/emoji/emoji-sequences.txt" -O unicode/emoji-sequences.txt
wget "${url}/${version}/emoji/emoji-zwj-sequences.txt" -O unicode/emoji-zwj-sequences.txt
