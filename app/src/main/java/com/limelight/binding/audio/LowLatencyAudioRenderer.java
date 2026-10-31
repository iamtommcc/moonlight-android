package com.limelight.binding.audio;

import android.content.Context;

import java.util.concurrent.atomic.AtomicLong;

import com.limelight.LimeLog;
import com.limelight.nvstream.av.audio.AudioRenderer;
import com.limelight.nvstream.jni.MoonBridge;

public class LowLatencyAudioRenderer implements AudioRenderer {
    // AAudio is experimental and must be explicitly enabled. Keep AudioTrack
    // as the default and as a fallback if AAudio setup fails.
    private final AndroidAudioRenderer audioTrackRenderer;
    private final boolean enableAudioFx;
    private final boolean forceAndroidLowLatency;
    private final int bufferMs;
    private volatile boolean useNativeAAudio;
    private final boolean diagnosticsEnabled;
    private final AtomicLong backlogDroppedFrames = new AtomicLong();
    private int channelCount;
    private int sampleRate;

    public LowLatencyAudioRenderer(Context context, boolean enableAudioFx,
                                   boolean forceAndroidLowLatency, int bufferMs, boolean diagnosticsEnabled) {
        this.enableAudioFx = enableAudioFx && !forceAndroidLowLatency;
        this.audioTrackRenderer = new AndroidAudioRenderer(context, this.enableAudioFx, diagnosticsEnabled);
        this.forceAndroidLowLatency = forceAndroidLowLatency;
        this.bufferMs = bufferMs;
        this.diagnosticsEnabled = diagnosticsEnabled;
    }

    @Override
    public int setup(MoonBridge.AudioConfiguration audioConfiguration, int sampleRate, int samplesPerFrame) {
        this.channelCount = audioConfiguration.channelCount;
        this.sampleRate = sampleRate;
        backlogDroppedFrames.set(0);
        if (forceAndroidLowLatency && NativeAAudioRenderer.isSupported()) {
            int result = NativeAAudioRenderer.setup(audioConfiguration, sampleRate, samplesPerFrame, bufferMs, diagnosticsEnabled);
            if (result == 0) {
                useNativeAAudio = true;
                return 0;
            }

            LimeLog.info("Native AAudio renderer setup failed; falling back to AudioTrack: " + result);
            NativeAAudioRenderer.cleanup();
        }
        else if (enableAudioFx) {
            LimeLog.info("Audio effects enabled; using AudioTrack renderer");
        }

        return audioTrackRenderer.setup(audioConfiguration, sampleRate, samplesPerFrame);
    }

    @Override
    public void start() {
        if (useNativeAAudio) {
            NativeAAudioRenderer.start();
        }
        else {
            audioTrackRenderer.start();
        }
    }

    @Override
    public void stop() {
        if (useNativeAAudio) {
            NativeAAudioRenderer.stop();
        }
        else {
            audioTrackRenderer.stop();
        }
    }

    @Override
    public void playDecodedAudio(short[] audioData) {
        if (MoonBridge.getPendingAudioDuration() >= 40) {
            if (diagnosticsEnabled && channelCount > 0) {
                backlogDroppedFrames.addAndGet(audioData.length / channelCount);
            }
            LimeLog.info("Too much pending audio data: " + MoonBridge.getPendingAudioDuration() + " ms");
            return;
        }

        if (useNativeAAudio) {
            NativeAAudioRenderer.playDecodedAudio(audioData);
        }
        else {
            audioTrackRenderer.playDecodedAudio(audioData);
        }
    }

    public AudioDiagnostics getDiagnostics() {
        long backlogDrops = backlogDroppedFrames.get();
        if (useNativeAAudio) {
            return AudioDiagnostics.fromAAudio(NativeAAudioRenderer.getDiagnostics(), sampleRate, backlogDrops);
        }
        return audioTrackRenderer.getDiagnostics(backlogDrops);
    }

    @Override
    public void cleanup() {
        if (useNativeAAudio) {
            NativeAAudioRenderer.cleanup();
            useNativeAAudio = false;
        }
        else {
            audioTrackRenderer.cleanup();
        }
    }
}
