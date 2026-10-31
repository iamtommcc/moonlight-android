# Experimental AAudio audio changes

This branch builds on Artemis (formerly Moonlight Noir), itself a fork of
[Moonlight Android](https://github.com/moonlight-stream/moonlight-android), and
[olibols's AAudio PR #567](https://github.com/ClassicOldSong/moonlight-android/pull/567)
at `fa0a309513bc48bf95a9a01ee3b2a14be9a1e04e`. The original PR author retains
authorship of that commit. The existing GPLv3 license and project credits apply.

## Problem and result

The original PR resolves excessive audio latency on some Google TV Streamers by
using AAudio. Its callback uses `pthread_mutex_trylock()` to read decoded PCM.
When the producer holds that mutex, the callback outputs silence even if there
is buffered audio available. Skipped consumption can subsequently fill the queue
and trigger PCM drops.

On a Google TV Streamer 4K using soundbar output, diagnostics showed lock misses,
silence and dropped frames increasing alongside repeated audible crackle, while
the pending-audio backlog-drop subtotal stayed zero and starvation was flat
after startup. After the lock-free update, the user reported no audible crackle
and zero dropped PCM frames after five minutes at the same buffer settings.
Silence increased slightly. These observations support the diagnosis, but are
not a controlled laboratory measurement or a claim about every audio device.

## Settings

Under **Audio Settings**:

- **Force Android Low Latency (Experimental)** opts into AAudio on Android 8+.
  It defaults off, bypasses system equalizer support, and falls back to AudioTrack
  if native setup fails.
- **Low latency audio buffer** offers Default and 8/12/16/24/32/48/64 ms targets.
  Android can round or clamp the requested playback buffer. This setting does
  not deliberately prebuffer that duration; the HUD shows the accepted size.
- **Show audio diagnostics** displays an independent bottom-left HUD, refreshed
  twice per second. It defaults off. Reconnect after changing these settings.

The lock-free change preserves the buffer defaults, selections, hardware target
calculation and software queue capacity calculation from the preceding build.
A larger queue can absorb bursts, but does not remove the original callback's
silence-on-lock-failure path.

## Queue design and overflow

The decode path is the single producer; AAudio's data callback is the single
consumer. Preallocated interleaved PCM storage is published and retired with
lock-free 32-bit acquire/release cursors. Separate physical offsets preserve
arbitrary buffer capacities across sequence-counter wraparound.

The callback performs bounded copies and zero-fills only missing audio. It does
not acquire a mutex, allocate, log, sleep, or spin. The producer/lifecycle mutex
remains to protect writes from setup/cleanup, but the callback never takes it.
Stop leaves the potentially active queue intact; cleanup closes the stream
before freeing its storage.

On overflow, the producer accepts the prefix that fits and drops incoming excess
PCM frames. It does not advance the consumer's cursor or overwrite queued PCM
that the callback may be reading. This differs from the original oldest-frame
eviction policy. The queue retains the same bounded capacity; genuine overflow
can still create a discontinuity and audible click.

## Diagnostics

Counters are session totals; frame counts mean PCM frames, not channel samples
or network packets. AAudio shows:

- Queue occupancy/capacity in milliseconds and percent, sampled concurrently.
- The actual configured Android output buffer duration, not end-to-end latency.
- Dropped frames from the PCM queue plus the existing pending-audio guard;
  the backlog subtotal identifies the latter.
- Starvation events/missing frames, and total silence frames zero-filled.
- Android-reported output underruns, when supported. They do not measure
  failures inside an HDMI receiver, TV, soundbar or eARC connection.

The renderer label is **AAudio (lock-free)**. Lock misses show **N/A**, because
there is no callback mutex to miss. That label alone is not evidence of an
improvement: compare audible glitches and changes in Dropped/Starvation/Silence.
AudioTrack exposes estimated queue occupancy, dropped frames and API underruns;
unsupported metrics show N/A. Startup starvation may be counted.

## Validation and limitations

- Release build 20.2.6, version code 60, installed on the Google TV Streamer.
- Nine targeted JVM/Robolectric preference and diagnostics tests passed.
- Native integration tests cover callback progress while the producer mutex is
  held, overflow/drop units, starvation, diagnostic polling, cleanup order and
  disabled counters under AddressSanitizer and UndefinedBehaviorSanitizer.
- Queue tests compare against an independent FIFO model for 1/2/6/8 channels,
  full/empty/partial operations, arbitrary capacities and 32-bit cursor wrap.
- Concurrent producer/consumer/observer tests transfer one million eight-channel
  frames with sample/order checks under ASan/UBSan and ThreadSanitizer.
- Release vital lint passes. Full release lint retains 13 pre-existing errors
  in unrelated API guards, a missing super call and a Vietnamese format string.

Surround-channel mapping, route changes, every Android version and long-duration
device playback have not been exhaustively validated. The AudioTrack fallback
remains available by disabling the experimental renderer.

## Build and tests

Use JDK 21, Android SDK 36, and NDK 27.0.12077973. Initialize submodules using
`git submodule update --init --recursive`, and configure your Android SDK path.

The Google TV Streamer build used the `armeabi-v7a` ABI:

```sh
./gradlew --no-daemon --max-workers=6 -I tools/streamer-abi.init.gradle \
  :app:assembleNonRoot_gameRelease \
  :app:testNonRoot_gameDebugUnitTest \
  --tests com.limelight.preferences.ExperimentalAudioPreferencesTest \
  --tests com.limelight.binding.audio.AudioDiagnosticsTest
```

The optional init script limits APK splits to that ABI and suppresses the
repository's existing ExtraTranslation resource issue. It does not suppress
the other full-lint failures. The release APK is unsigned; use your own signing
key. Local device pairing and signing credentials are not part of the repository.

With `JAVA_HOME` and `ANDROID_HOME` configured, run the native host tests on macOS
or Linux with Clang:

```sh
bash app/src/test/native/run_audio_tests.sh
```

Set `RUN_TSAN=0` only if the host does not support ThreadSanitizer.
