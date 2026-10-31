#include "../../main/jni/moonlight-core/spsc_audio_queue.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t randomState = 7;
static uint32_t nextRandom(void) {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}

static int16_t frameSample(uint32_t frame, uint32_t channel) {
    return (int16_t)((frame * 13 + channel * 97) & 0x7fff);
}

// Compare against an independent sequential FIFO model, including rejected
// writes. Preserve entire multichannel frames through uneven read/write chunks.
static void testAgainstModel(uint32_t capacity, uint32_t channels) {
    int16_t* storage = calloc(capacity * channels, sizeof(*storage));
    int16_t* model = calloc(capacity * channels, sizeof(*model));
    assert(storage && model);
    SpscAudioQueue queue = {0};
    spscAudioQueueInit(&queue, storage, capacity, channels);
    // Force counter and physical-buffer wrap independently, including non-power
    // of two capacities. Sequence arithmetic must survive a 32-bit wrap.
    atomic_store(&queue.readCounter, UINT32_MAX - 23);
    atomic_store(&queue.writeCounter, UINT32_MAX - 23);
    queue.readOffset = queue.writeOffset = capacity - 1;
    uint32_t head = 0, used = 0, inputFrame = 0;
    for (int step = 0; step < 20000; step++) {
        uint32_t frames = nextRandom() % 97 + 1;
        int16_t samples[97 * 8];
        if (nextRandom() & 1) {
            for (uint32_t i = 0; i < frames; i++) {
                for (uint32_t c = 0; c < channels; c++) samples[i * channels + c] = frameSample(inputFrame + i, c);
            }
            uint32_t accepted = frames < capacity - used ? frames : capacity - used;
            assert(spscAudioQueueWrite(&queue, samples, frames) == accepted);
            for (uint32_t i = 0; i < accepted; i++) {
                uint32_t slot = (head + used + i) % capacity;
                memcpy(model + slot * channels, samples + i * channels, channels * sizeof(*samples));
            }
            used += accepted;
            inputFrame += frames;
        }
        else {
            uint32_t expected = frames < used ? frames : used;
            assert(spscAudioQueueRead(&queue, samples, frames) == expected);
            for (uint32_t i = 0; i < expected; i++) {
                assert(memcmp(samples + i * channels, model + ((head + i) % capacity) * channels,
                              channels * sizeof(*samples)) == 0);
            }
            head = (head + expected) % capacity;
            used -= expected;
        }
        assert(spscAudioQueueOccupancy(&queue) == used);
    }
    free(storage);
    free(model);
}

enum { STRESS_FRAMES = 1000000, STRESS_CHANNELS = 8 };
static SpscAudioQueue concurrentQueue;
static atomic_bool consumerDone;

static void* produce(void* ignored) {
    (void)ignored;
    uint32_t sent = 0;
    while (sent < STRESS_FRAMES) {
        uint32_t count = STRESS_FRAMES - sent;
        if (count > 31) count = 31;
        int16_t samples[31 * STRESS_CHANNELS];
        for (uint32_t i = 0; i < count; i++) {
            for (uint32_t c = 0; c < STRESS_CHANNELS; c++) samples[i * STRESS_CHANNELS + c] = frameSample(sent + i, c);
        }
        uint32_t accepted = spscAudioQueueWrite(&concurrentQueue, samples, count);
        sent += accepted;
        if (accepted == 0) sched_yield(); // Test driver only; the real producer drops excess.
    }
    return NULL;
}

static void* consume(void* ignored) {
    (void)ignored;
    uint32_t received = 0;
    while (received < STRESS_FRAMES) {
        int16_t samples[47 * STRESS_CHANNELS];
        uint32_t count = spscAudioQueueRead(&concurrentQueue, samples, 47);
        for (uint32_t i = 0; i < count; i++) {
            for (uint32_t c = 0; c < STRESS_CHANNELS; c++) {
                assert(samples[i * STRESS_CHANNELS + c] == frameSample(received + i, c));
            }
        }
        received += count;
        if (count == 0) sched_yield();
    }
    atomic_store(&consumerDone, true);
    return NULL;
}

static void testConcurrentProducerConsumerAndObserver(void) {
    uint32_t capacity = 1153; // Deliberately not a power of two.
    int16_t* storage = calloc(capacity * STRESS_CHANNELS, sizeof(*storage));
    assert(storage);
    spscAudioQueueInit(&concurrentQueue, storage, capacity, STRESS_CHANNELS);
    atomic_store(&concurrentQueue.readCounter, UINT32_MAX - 1000);
    atomic_store(&concurrentQueue.writeCounter, UINT32_MAX - 1000);
    pthread_t producer, consumer;
    assert(pthread_create(&producer, NULL, produce, NULL) == 0);
    assert(pthread_create(&consumer, NULL, consume, NULL) == 0);
    while (!atomic_load(&consumerDone)) {
        assert(spscAudioQueueOccupancy(&concurrentQueue) <= capacity);
        sched_yield();
    }
    assert(pthread_join(producer, NULL) == 0);
    assert(pthread_join(consumer, NULL) == 0);
    assert(spscAudioQueueOccupancy(&concurrentQueue) == 0);
    free(storage);
}

int main(void) {
    uint32_t capacities[] = {1, 7, 960, 1152};
    uint32_t channels[] = {1, 2, 6, 8};
    for (uint32_t i = 0; i < 4; i++) {
        for (uint32_t j = 0; j < 4; j++) testAgainstModel(capacities[i], channels[j]);
    }
    testConcurrentProducerConsumerAndObserver();
    puts("PASS: FIFO model, overflow, PCM channel alignment, wraparound, and 1M-frame concurrency");
    return 0;
}
