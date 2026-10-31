#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
test_output_dir="$(mktemp -d "${TMPDIR:-/tmp}/moonlight-audio-tests.XXXXXX")"
trap 'rm -rf "$test_output_dir"' EXIT

: "${JAVA_HOME:?Set JAVA_HOME to a JDK}"
if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
    : "${ANDROID_HOME:?Set ANDROID_HOME or ANDROID_NDK_HOME}"
    ANDROID_NDK_HOME="$ANDROID_HOME/ndk/27.0.12077973"
fi

case "$(uname -s)" in
    Darwin) jni_platform=darwin; link_flags=(-pthread) ;;
    Linux) jni_platform=linux; link_flags=(-pthread -ldl) ;;
    *) echo "Native audio tests support macOS and Linux hosts." >&2; exit 1 ;;
esac

ndk_include=""
for candidate in "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/sysroot/usr/include; do
    if [[ -f "$candidate/aaudio/AAudio.h" ]]; then ndk_include="$candidate"; break; fi
done
[[ -n "$ndk_include" ]] || { echo "AAudio headers not found in $ANDROID_NDK_HOME" >&2; exit 1; }

clang -std=c11 -O1 -g -fsanitize=address,undefined \
    -Wno-availability -Wno-nullability-completeness \
    -I "$JAVA_HOME/include" -I "$JAVA_HOME/include/$jni_platform" \
    -idirafter "$ndk_include" "$test_dir/aaudio_diagnostics_test.c" \
    "${link_flags[@]}" -o "$test_output_dir/renderer-test"
"$test_output_dir/renderer-test"

clang -std=c11 -O1 -g -fsanitize=address,undefined \
    "$test_dir/spsc_audio_queue_test.c" -pthread -o "$test_output_dir/queue-test"
"$test_output_dir/queue-test"

if [[ "${RUN_TSAN:-1}" != 0 ]]; then
    clang -std=c11 -O1 -g -fsanitize=thread \
        "$test_dir/spsc_audio_queue_test.c" -pthread -o "$test_output_dir/queue-tsan-test"
    "$test_output_dir/queue-tsan-test"
fi
