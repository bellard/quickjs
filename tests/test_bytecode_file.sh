#!/bin/sh
set -eu

QJS="${QJS:-./qjs}"
QJSC="${QJSC:-./qjsc}"
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
