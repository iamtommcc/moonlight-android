package com.limelight.binding.audio;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;

import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
@Config(sdk = 33, qualifiers = "en-rUS", shadows = {com.limelight.shadows.ShadowMoonBridge.class,
        com.limelight.shadows.ShadowGameManager.class})
public class AudioDiagnosticsTest {
    private final Context context = ApplicationProvider.getApplicationContext();

    @Test
    public void snapshotUsesPcmFramesForMillisecondsAndIncludesBothDropSources() {
        AudioDiagnostics snapshot = AudioDiagnostics.fromAAudio(
                new long[]{480, 1920, 960, 96, 2, 192, 3, 4, 768}, 48000, 48);
        String text = snapshot.format(context);
        assertTrue(text.contains("10.0 / 40.0 ms (25%)"));
        assertTrue(text.contains("Output buffer: 20.0 ms"));
        assertTrue(text.contains("Dropped: 144 PCM frames (backlog: 48)"));
        assertTrue(text.contains("Starvation: 2 (192 frames) | lock misses: 3"));
        assertTrue(text.contains("HW/API underruns: 4 | silence: 768 frames"));
    }

    @Test
    public void unsupportedUnderrunsAreNeverReportedAsZero() {
        AudioDiagnostics snapshot = AudioDiagnostics.fromAAudio(
                new long[]{0, 1920, 960, 0, 0, 0, 0, -895, 0}, 48000, 0);
        assertTrue(snapshot.format(context).contains("HW/API underruns: N/A"));
    }

    @Test
    public void lockFreeRendererDoesNotPretendToCountMutexMisses() {
        AudioDiagnostics snapshot = AudioDiagnostics.fromAAudio(
                new long[]{0, 1920, 960, 0, 0, 0, -1, 0, 0}, 48000, 0);
        String text = snapshot.format(context);
        assertTrue(text.contains("AAudio (lock-free)"));
        assertTrue(text.contains("lock misses: N/A"));
        assertTrue(text.contains("silence: 0 frames"));
    }

    @Test
    public void closedStreamAndAudioTrackOnlyCountersRemainExplicitlyUnavailable() {
        String closed = AudioDiagnostics.fromAAudio(null, 48000, 24).format(context);
        assertTrue(closed.contains("queue: N/A"));
        assertTrue(closed.contains("Dropped: 24 PCM frames"));
        assertTrue(closed.contains("Starvation: N/A | lock misses: N/A"));
        String track = new AudioDiagnostics("AudioTrack (~queue)", 48000, 240, 960, 960,
                0, 0, -1, -1, -1, 2, -1).format(context);
        assertTrue(track.contains("5.0 / 20.0 ms (25%)"));
        assertTrue(track.contains("HW/API underruns: 2 | silence: N/A"));
    }

    @Test
    public void largeUnsignedNativeCountersRemainPositive() {
        AudioDiagnostics snapshot = AudioDiagnostics.fromAAudio(
                new long[]{0, 1920, 960, 0xffffffffL, 0, 0, 0, 0, 0}, 48000, 0);
        assertEquals(4294967295L, snapshot.droppedFrames);
    }
}
