package com.limelight.binding.audio;

import android.os.Build;

import com.limelight.LimeLog;
import com.limelight.nvstream.jni.MoonBridge;

final class NativeAAudioRenderer {
    private NativeAAudioRenderer() {}

    private static final boolean SUPPORTED = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && nativeIsSupported();

    static boolean isSupported() {
        return SUPPORTED;
    }

    static synchronized int setup(MoonBridge.AudioConfiguration audioConfiguration, int sampleRate, int samplesPerFrame, int bufferMs, boolean diagnosticsEnabled) {
        if (!SUPPORTED) {
            return -1;
        }

        int result = nativeSetup(audioConfiguration.channelCount, sampleRate, samplesPerFrame, bufferMs, diagnosticsEnabled);
        if (result == 0) {
            LimeLog.info("Using native AAudio renderer for low-latency audio");
        }
        return result;
    }

    static synchronized void start() {
        nativeStart();
    }

    static synchronized void stop() {
        nativeStop();
    }

    static void playDecodedAudio(short[] audioData) {
        nativeWrite(audioData, audioData.length);
    }

    static synchronized void cleanup() {
        nativeCleanup();
    }

    // Serialize polling with stream lifecycle, never with the realtime callback.
    static synchronized long[] getDiagnostics() {
        return nativeGetDiagnostics();
    }

    private static native long[] nativeGetDiagnostics();
    private static native boolean nativeIsSupported();
    private static native int nativeSetup(int channelCount, int sampleRate, int samplesPerFrame, int bufferMs, boolean diagnosticsEnabled);
    private static native void nativeStart();
    private static native void nativeStop();
    private static native void nativeWrite(short[] audioData, int sampleCount);
    private static native void nativeCleanup();
}
