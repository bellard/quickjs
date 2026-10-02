#!/bin/sh
set -eu

QJS="${QJS:-./qjs}"
QJSC="${QJSC:-./qjsc}"
QBC_REGEXP_MUTATOR="${QBC_REGEXP_MUTATOR:-./tests/qbc_regexp_mutator}"
TMP_DIR="${TMPDIR:-/tmp}/quickjs-bytecode-file-test-$$"

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT INT TERM

mkdir -p "$TMP_DIR"

cat > "$TMP_DIR/hello.js" <<'EOF'
var name = scriptArgs[1] || "world";
console.log("hello " + name);
EOF

"$QJSC" --bytecode -o "$TMP_DIR/hello.qbc" "$TMP_DIR/hello.js"

if [ ! -s "$TMP_DIR/hello.qbc" ]; then
    echo "bytecode file was not created" >&2
    exit 1
fi

"$QJS" --bytecode "$TMP_DIR/hello.qbc" QuickJS > "$TMP_DIR/out.txt"
if ! grep -q '^hello QuickJS$' "$TMP_DIR/out.txt"; then
    echo "bytecode execution produced unexpected output" >&2
    cat "$TMP_DIR/out.txt" >&2
    exit 1
fi

cat > "$TMP_DIR/module.mjs" <<'EOF'
await 0;
console.log("module bytecode ok");
EOF

"$QJSC" --bytecode -m -o "$TMP_DIR/module.qbc" "$TMP_DIR/module.mjs"
"$QJS" --bytecode "$TMP_DIR/module.qbc" > "$TMP_DIR/module.out"
if ! grep -q '^module bytecode ok$' "$TMP_DIR/module.out"; then
    echo "module bytecode execution produced unexpected output" >&2
    cat "$TMP_DIR/module.out" >&2
    exit 1
fi

dd if=/dev/zero of="$TMP_DIR/not-bytecode.qbc" bs=1 count=64 2>/dev/null
if "$QJS" --bytecode "$TMP_DIR/not-bytecode.qbc" > "$TMP_DIR/not-bytecode.out" 2> "$TMP_DIR/not-bytecode.err"; then
    echo "qjs accepted a non-bytecode file" >&2
    exit 1
fi
if ! grep -q 'bad magic' "$TMP_DIR/not-bytecode.err"; then
    echo "missing bad magic diagnostic" >&2
    cat "$TMP_DIR/not-bytecode.err" >&2
    exit 1
fi

cp "$TMP_DIR/hello.qbc" "$TMP_DIR/bad-version.qbc"
printf '0' | dd of="$TMP_DIR/bad-version.qbc" bs=1 seek=12 count=1 conv=notrunc 2>/dev/null
if "$QJS" --bytecode "$TMP_DIR/bad-version.qbc" > "$TMP_DIR/bad-version.out" 2> "$TMP_DIR/bad-version.err"; then
    echo "qjs accepted a bytecode file with a corrupted version" >&2
    exit 1
fi
if ! grep -q 'version mismatch' "$TMP_DIR/bad-version.err"; then
    echo "missing version mismatch diagnostic" >&2
    cat "$TMP_DIR/bad-version.err" >&2
    exit 1
fi

cp "$TMP_DIR/hello.qbc" "$TMP_DIR/bad-checksum.qbc"
dd if=/dev/zero of="$TMP_DIR/bad-checksum.qbc" bs=1 seek=52 count=8 conv=notrunc 2>/dev/null
if "$QJS" --bytecode "$TMP_DIR/bad-checksum.qbc" > "$TMP_DIR/bad-checksum.out" 2> "$TMP_DIR/bad-checksum.err"; then
    echo "qjs accepted a bytecode file with a corrupted payload" >&2
    exit 1
fi
if ! grep -q 'checksum mismatch' "$TMP_DIR/bad-checksum.err"; then
    echo "missing checksum mismatch diagnostic" >&2
    cat "$TMP_DIR/bad-checksum.err" >&2
    exit 1
fi

cat > "$TMP_DIR/regexp-validation.js" <<'EOF'
var results = [
    /abc/.exec("zabc")[0],
    /\x61bc/.exec("zabc")[0],
    /(abc)/.exec("zabc")[1],
    /a.c/.exec("za-c")[0],
    /abcdef[0-9]+/.exec("zabcdef1")[0],
    /[a-z]bcdef/.exec("zxbcdef")[0],
    /(^|[^\\])"x"/.exec('00"x"')[0],
];
console.log(results.join(","));
console.log("regexp qbc validation sentinel");
EOF

"$QJSC" -s --bytecode -o "$TMP_DIR/regexp-validation.qbc" \
    "$TMP_DIR/regexp-validation.js"
cat > "$TMP_DIR/regexp-function-validation.js" <<'EOF'
console.log(/abc/.test("abc"));
EOF
"$QJSC" -s --bytecode -o "$TMP_DIR/regexp-function-validation.qbc" \
    "$TMP_DIR/regexp-function-validation.js"

"$QBC_REGEXP_MUTATOR" "$TMP_DIR/regexp-validation.qbc" \
    "$TMP_DIR/regexp-clear-atom.qbc" clear-atom
"$QJS" --bytecode "$TMP_DIR/regexp-clear-atom.qbc" \
    > "$TMP_DIR/regexp-clear-atom.out"
if ! grep -q '^abc,abc,abc,a-c,abcdef1,xbcdef,0"x"$' "$TMP_DIR/regexp-clear-atom.out"; then
    echo "qjs rejected or misexecuted a safe missing ATOM marker" >&2
    cat "$TMP_DIR/regexp-clear-atom.out" >&2
    exit 1
fi

for mode in forge-atom-capture forge-atom-nonliteral bad-regexp-opcode \
    bad-metadata-version bad-source-payload bad-scan-entry \
    bad-leading-char bad-prefix-entry bad-quick-check bad-function-opcode; do
    mutation_input="$TMP_DIR/regexp-validation.qbc"
    if [ "$mode" = bad-function-opcode ]; then
        mutation_input="$TMP_DIR/regexp-function-validation.qbc"
    fi
    "$QBC_REGEXP_MUTATOR" "$mutation_input" \
        "$TMP_DIR/$mode.qbc" "$mode"
    status=0
    "$QJS" --bytecode "$TMP_DIR/$mode.qbc" \
        > "$TMP_DIR/$mode.out" 2> "$TMP_DIR/$mode.err" || status=$?
    if [ "$status" -eq 0 ]; then
        echo "qjs accepted malformed bytecode mode: $mode" >&2
        exit 1
    fi
    if [ "$status" -ge 128 ]; then
        echo "qjs crashed on malformed bytecode mode: $mode" >&2
        cat "$TMP_DIR/$mode.err" >&2
        exit 1
    fi
    if grep -q 'regexp qbc validation sentinel' "$TMP_DIR/$mode.out"; then
        echo "qjs evaluated malformed bytecode mode: $mode" >&2
        exit 1
    fi
    if ! grep -Eq 'invalid (regexp|function)' "$TMP_DIR/$mode.err"; then
        echo "missing validation diagnostic for mode: $mode" >&2
        cat "$TMP_DIR/$mode.err" >&2
        exit 1
    fi
done

"$QBC_REGEXP_MUTATOR" "$TMP_DIR/regexp-validation.qbc" \
    "$TMP_DIR/old-bytecode-version.qbc" old-bytecode-version
if "$QJS" --bytecode "$TMP_DIR/old-bytecode-version.qbc" \
    > "$TMP_DIR/old-bytecode-version.out" \
    2> "$TMP_DIR/old-bytecode-version.err"; then
    echo "qjs accepted a prior bytecode version" >&2
    exit 1
fi
if ! grep -q 'invalid version' "$TMP_DIR/old-bytecode-version.err"; then
    echo "missing prior bytecode version diagnostic" >&2
    cat "$TMP_DIR/old-bytecode-version.err" >&2
    exit 1
fi
