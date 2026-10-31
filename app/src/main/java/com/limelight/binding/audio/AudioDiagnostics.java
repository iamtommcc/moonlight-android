package com.limelight.binding.audio;

import android.content.Context;

import com.limelight.R;

/** One UI snapshot. Frame counts are PCM frames, not packets or channel samples. */
public final class AudioDiagnostics {
    public final String renderer;
    public final int sampleRate;
    public final long queuedFrames;
    public final int capacityFrames;
    public final int outputBufferFrames;
    public final long droppedFrames;
    public final long backlogDroppedFrames;
    public final long starvationEvents;
    public final long starvationFrames;
    public final long lockMisses;
    public final long underruns;
    public final long silenceFrames;

    AudioDiagnostics(String renderer, int sampleRate, long queuedFrames, int capacityFrames,
                     int outputBufferFrames, long droppedFrames, long backlogDroppedFrames,
                     long starvationEvents, long starvationFrames, long lockMisses,
                     long underruns, long silenceFrames) {
        this.renderer = renderer;
        this.sampleRate = sampleRate;
        this.queuedFrames = queuedFrames;
        this.capacityFrames = capacityFrames;
        this.outputBufferFrames = outputBufferFrames;
        this.droppedFrames = droppedFrames;
        this.backlogDroppedFrames = backlogDroppedFrames;
        this.starvationEvents = starvationEvents;
        this.starvationFrames = starvationFrames;
        this.lockMisses = lockMisses;
        this.underruns = underruns;
        this.silenceFrames = silenceFrames;
    }

    static AudioDiagnostics unavailable(String renderer, int sampleRate, long backlogDrops) {
        return new AudioDiagnostics(renderer, sampleRate, -1, -1, -1, backlogDrops,
                backlogDrops, -1, -1, -1, -1, -1);
    }

    static AudioDiagnostics fromAAudio(long[] values, int sampleRate, long backlogDrops) {
        // Order matches nativeGetDiagnostics(). Missing/closed streams are not zero glitches.
        if (values == null || values.length != 9) {
            return unavailable("AAudio (lock-free)", sampleRate, backlogDrops);
        }
        return new AudioDiagnostics("AAudio (lock-free)", sampleRate, values[0], (int) values[1],
                (int) values[2], values[3] + backlogDrops, backlogDrops,
                values[4], values[5], values[6], values[7], values[8]);
    }

    public String format(Context context) {
        String na = context.getString(R.string.audio_diagnostics_unavailable);
        String queue = queuedFrames >= 0 && capacityFrames > 0 && sampleRate > 0
                ? context.getString(R.string.audio_diagnostics_queue,
                    queuedFrames * 1000.0 / sampleRate, capacityFrames * 1000.0 / sampleRate,
                    Math.round(queuedFrames * 100.0 / capacityFrames)) : na;
        String output = outputBufferFrames > 0 && sampleRate > 0
                ? context.getString(R.string.audio_diagnostics_duration,
                    outputBufferFrames * 1000.0 / sampleRate) : na;
        String starvation = starvationEvents >= 0
                ? context.getString(R.string.audio_diagnostics_starvation, starvationEvents, starvationFrames) : na;
        String silence = silenceFrames >= 0
                ? context.getString(R.string.audio_diagnostics_frames, silenceFrames) : na;
        return context.getString(R.string.audio_diagnostics_overlay, renderer, queue, output,
                droppedFrames, backlogDroppedFrames, starvation, lockMisses < 0 ? na : Long.toString(lockMisses),
                underruns < 0 ? na : Long.toString(underruns), silence);
    }
}
