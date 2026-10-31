// Host test of the actual callback/write/snapshot implementation, without an
// Android device. AAudio stream creation is not exercised by this harness.
#ifndef __ANDROID__
#define __INTRODUCED_IN(x)
#define __INTRODUCED_IN_32(x)
#define __INTRODUCED_IN_64(x)
#endif
#include "../../main/jni/moonlight-core/aaudio_renderer.c"
#include <assert.h>
#include <stdio.h>

int __android_log_print(int priority, const char* tag, const char* format, ...) {
    (void)priority; (void)tag; (void)format;
    return 0;
}

typedef struct {
    jsize length;
    jshort* data;
} TestAudio;
static jlong snapshot[9];
static jsize getArrayLength(JNIEnv* env, jarray array) {
    (void)env;
    return ((TestAudio*)array)->length;
}
static jshort* getShortArrayElements(JNIEnv* env, jshortArray array, jboolean* copy) {
    (void)env; (void)copy;
    return ((TestAudio*)array)->data;
}
static void releaseShortArrayElements(JNIEnv* env, jshortArray array, jshort* data, jint mode) {
    (void)env; (void)array; (void)data; (void)mode;
}
static jlongArray newLongArray(JNIEnv* env, jsize size) {
    (void)env;
    assert(size == 9);
    return (jlongArray)snapshot;
}
static void setLongArrayRegion(JNIEnv* env, jlongArray array, jsize start, jsize size, const jlong* data) {
    (void)env;
    memcpy((jlong*)array + start, data, size * sizeof(jlong));
}
static const struct JNINativeInterface_ jniFunctions = {
    .GetArrayLength = getArrayLength,
    .GetShortArrayElements = getShortArrayElements,
    .ReleaseShortArrayElements = releaseShortArrayElements,
    .NewLongArray = newLongArray,
    .SetLongArrayRegion = setLongArrayRegion,
};
static JNIEnv testEnv = &jniFunctions;

static void setupRing(int channels, int capacityFrames, bool enabled) {
    freeQueue();
    resetDiagnostics();
    channelCount = channels;
    int16_t* storage = calloc(capacityFrames * channels, sizeof(*storage));
    assert(storage != NULL);
    spscAudioQueueInit(&audioQueue, storage, capacityFrames, channels);
    diagnosticsEnabled = enabled;
    diagnosticCapacityFrames = capacityFrames;
    diagnosticOutputBufferFrames = capacityFrames / 2;
}

static void writeSamples(jshort* samples, int count) {
    TestAudio array = {count, samples};
    Java_com_limelight_binding_audio_NativeAAudioRenderer_nativeWrite(&testEnv, NULL, (jshortArray)&array, count);
}

static void testCallbackReadsEvenWhileProducerMutexIsHeld(void) {
    setupRing(2, 8, true);
    jshort samples[] = {1, 2, 3, 4, 5, 6};
    jshort output[6];
    writeSamples(samples, 6);
    assert(spscAudioQueueOccupancy(&audioQueue) == 3);
    pthread_mutex_lock(&writerMutex);
    dataCallback(NULL, NULL, output, 2);
    pthread_mutex_unlock(&writerMutex);
    assert(memcmp(samples, output, 4 * sizeof(jshort)) == 0);
    assert(spscAudioQueueOccupancy(&audioQueue) == 1);
    assert(atomic_load(&diagnosticStarvationEvents) == 0);
    assert(atomic_load(&diagnosticSilenceFrames) == 0);

    dataCallback(NULL, NULL, output, 3);
    assert(output[0] == 5 && output[1] == 6);
    for (int i = 2; i < 6; i++) assert(output[i] == 0);
    assert(atomic_load(&diagnosticStarvationEvents) == 1);
    assert(atomic_load(&diagnosticStarvationFrames) == 2);
    assert(atomic_load(&diagnosticSilenceFrames) == 2);
    assert(spscAudioQueueOccupancy(&audioQueue) == 0);
}

static void testOverflowRejectsIncomingExcessInPcmFrames(void) {
    setupRing(2, 4, true);
    jshort first[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    jshort second[] = {13, 14, 15, 16, 17, 18};
    writeSamples(first, 12); // Accepts four frames, rejects the trailing two.
    assert(atomic_load(&diagnosticDroppedFrames) == 2);
    writeSamples(second, 6); // Rejects all three while full; queued data stays valid.
    assert(atomic_load(&diagnosticDroppedFrames) == 5);
    assert(spscAudioQueueOccupancy(&audioQueue) == 4);
    jshort output[8];
    dataCallback(NULL, NULL, output, 4);
    assert(memcmp(output, first, sizeof(output)) == 0);
    assert(atomic_load(&diagnosticStarvationEvents) == 0);
}

static int32_t getXRunCount(AAudioStream* ignored) {
    (void)ignored;
    return 7;
}

static void testSnapshotsRemainNonblockingAndLockMissesAreUnavailable(void) {
    setupRing(6, 24, true);
    stream = (AAudioStream*)(uintptr_t)1;
    p_AAudioStream_getXRunCount = getXRunCount;
    atomic_store(&diagnosticDroppedFrames, UINT32_MAX);
    pthread_mutex_lock(&writerMutex);
    assert(Java_com_limelight_binding_audio_NativeAAudioRenderer_nativeGetDiagnostics(&testEnv, NULL) != NULL);
    pthread_mutex_unlock(&writerMutex);
    assert(snapshot[1] == 24 && snapshot[2] == 12);
    assert(snapshot[3] == 4294967295LL);
    assert(snapshot[6] == -1);
    assert(snapshot[7] == 7);
    p_AAudioStream_getXRunCount = NULL;
    Java_com_limelight_binding_audio_NativeAAudioRenderer_nativeGetDiagnostics(&testEnv, NULL);
    assert(snapshot[7] == -1);
    stream = NULL;
    assert(Java_com_limelight_binding_audio_NativeAAudioRenderer_nativeGetDiagnostics(&testEnv, NULL) == NULL);
}

static aaudio_result_t requestStop(AAudioStream* ignored) {
    (void)ignored;
    assert(audioQueue.samples != NULL);
    return AAUDIO_OK;
}

static aaudio_result_t closeStream(AAudioStream* ignored) {
    (void)ignored;
    // close may finish an in-flight callback. Its storage must still exist.
    assert(audioQueue.samples != NULL);
    jshort output[2];
    dataCallback(NULL, NULL, output, 1);
    assert(output[0] == 1 && output[1] == 2);
    return AAUDIO_OK;
}

static void testStopDoesNotResetActiveQueueAndCleanupClosesBeforeFree(void) {
    setupRing(2, 4, true);
    stream = (AAudioStream*)(uintptr_t)1;
    p_AAudioStream_requestStop = requestStop;
    p_AAudioStream_close = closeStream;
    jshort samples[] = {1, 2, 3, 4};
    writeSamples(samples, 4);
    Java_com_limelight_binding_audio_NativeAAudioRenderer_nativeStop(&testEnv, NULL);
    assert(spscAudioQueueOccupancy(&audioQueue) == 2);
    Java_com_limelight_binding_audio_NativeAAudioRenderer_nativeCleanup(&testEnv, NULL);
    assert(stream == NULL && audioQueue.samples == NULL);
    // A final producer write after stop/cleanup is harmless.
    writeSamples(samples, 4);
}

static void testDisabledDiagnosticsAndReset(void) {
    setupRing(2, 4, false);
    jshort samples[] = {1, 2, 3, 4};
    jshort output[8];
    writeSamples(samples, 4);
    dataCallback(NULL, NULL, output, 4);
    assert(atomic_load(&diagnosticStarvationEvents) == 0);
    assert(atomic_load(&diagnosticSilenceFrames) == 0);
    assert(atomic_load(&diagnosticDroppedFrames) == 0);
    assert(output[0] == 1 && output[4] == 0);
    diagnosticsEnabled = true;
    dataCallback(NULL, NULL, output, 4);
    assert(atomic_load(&diagnosticStarvationEvents) == 1);
    resetDiagnostics();
    assert(atomic_load(&diagnosticStarvationEvents) == 0);
    assert(atomic_load(&diagnosticStarvationFrames) == 0);
    assert(atomic_load(&diagnosticSilenceFrames) == 0);
}

int main(void) {
    testCallbackReadsEvenWhileProducerMutexIsHeld();
    testOverflowRejectsIncomingExcessInPcmFrames();
    testSnapshotsRemainNonblockingAndLockMissesAreUnavailable();
    testStopDoesNotResetActiveQueueAndCleanupClosesBeforeFree();
    testDisabledDiagnosticsAndReset();
    freeQueue();
    puts("PASS: callback without mutex, starvation, overflow, diagnostics, lifecycle, and opt-out");
    return 0;
}
