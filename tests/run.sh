#!/bin/bash
# Build and run the protocol tests under AddressSanitizer/UBSan and ThreadSanitizer.
set -euo pipefail
cd "$(dirname "$0")/.."
SRC="Client/Headphones.cpp Client/HeadphonesDeviceSettings.cpp Client/HeadphonesPlayback.cpp Client/Listener.cpp Client/BluetoothWrapper.cpp Client/CommandSerializer.cpp Client/ByteMagic.cpp"
OUT=$(mktemp -d)
status=0
for san in address,undefined thread; do
    clang++ -std=c++17 -g -fsanitize=$san -IClient tests/state_sync_test.cpp $SRC -o "$OUT/test" 2>&1 | grep -v "warning:" || true
    if "$OUT/test" > "$OUT/log" 2>&1 && ! grep -qE "ThreadSanitizer|runtime error|AddressSanitizer" "$OUT/log"; then
        echo "[$san] $(grep -c '^ok' "$OUT/log") checks passed"
    else
        echo "[$san] FAILED"; grep -E "FAIL|Sanitizer|runtime error" "$OUT/log" | head -20; status=1
    fi
done
rm -rf "$OUT"
exit $status
