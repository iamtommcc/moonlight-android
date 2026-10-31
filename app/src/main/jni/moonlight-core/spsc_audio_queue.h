#ifndef MOONLIGHT_SPSC_AUDIO_QUEUE_H
#define MOONLIGHT_SPSC_AUDIO_QUEUE_H

#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Audio queue requires lock-free int atomics");
_Static_assert(sizeof(unsigned int) == sizeof(uint32_t), "Audio queue requires 32-bit counters");

// Exactly one producer and one consumer. Storage and configuration are immutable
// while either thread is active. Initialize/reset/free only when both are idle.
// The producer publishes samples with release; the consumer acquires them. The
// consumer releases storage only after copying, so the producer cannot overwrite
// a frame being read. Neither operation allocates, locks, spins, or waits.
typedef struct {
    int16_t* samples;
    uint32_t capacityFrames;
    uint32_t channels;
    _Alignas(64) atomic_uint readCounter;
    uint32_t readOffset;
    _Alignas(64) atomic_uint writeCounter;
    uint32_t writeOffset;
} SpscAudioQueue;

static inline void spscAudioQueueInit(SpscAudioQueue* queue, int16_t* storage,
                                      uint32_t capacityFrames, uint32_t channels) {
    queue->samples = storage;
    queue->capacityFrames = capacityFrames;
    queue->channels = channels;
    queue->readOffset = queue->writeOffset = 0;
    atomic_store_explicit(&queue->readCounter, 0, memory_order_relaxed);
    atomic_store_explicit(&queue->writeCounter, 0, memory_order_relaxed);
}

static inline uint32_t spscAudioQueueWrite(SpscAudioQueue* queue, const int16_t* samples,
                                           uint32_t frames) {
    uint32_t write = atomic_load_explicit(&queue->writeCounter, memory_order_relaxed);
    uint32_t read = atomic_load_explicit(&queue->readCounter, memory_order_acquire);
    uint32_t freeFrames = queue->capacityFrames - (uint32_t)(write - read);
    uint32_t count = frames < freeFrames ? frames : freeFrames;
    if (count == 0) return 0;

    uint32_t first = queue->capacityFrames - queue->writeOffset;
    if (first > count) first = count;
    memcpy(queue->samples + (size_t)queue->writeOffset * queue->channels, samples,
           (size_t)first * queue->channels * sizeof(*samples));
    if (first < count) {
        memcpy(queue->samples, samples + (size_t)first * queue->channels,
               (size_t)(count - first) * queue->channels * sizeof(*samples));
    }
    queue->writeOffset = (queue->writeOffset + count) % queue->capacityFrames;
    atomic_store_explicit(&queue->writeCounter, write + count, memory_order_release);
    return count;
}

static inline uint32_t spscAudioQueueRead(SpscAudioQueue* queue, int16_t* samples,
                                          uint32_t frames) {
    uint32_t read = atomic_load_explicit(&queue->readCounter, memory_order_relaxed);
    uint32_t write = atomic_load_explicit(&queue->writeCounter, memory_order_acquire);
    uint32_t available = write - read;
    uint32_t count = frames < available ? frames : available;
    if (count == 0) return 0;

    uint32_t first = queue->capacityFrames - queue->readOffset;
    if (first > count) first = count;
    memcpy(samples, queue->samples + (size_t)queue->readOffset * queue->channels,
           (size_t)first * queue->channels * sizeof(*samples));
    if (first < count) {
        memcpy(samples + (size_t)first * queue->channels, queue->samples,
               (size_t)(count - first) * queue->channels * sizeof(*samples));
    }
    queue->readOffset = (queue->readOffset + count) % queue->capacityFrames;
    atomic_store_explicit(&queue->readCounter, read + count, memory_order_release);
    return count;
}

// For diagnostics only: concurrent snapshots can overestimate occupancy. Clamp
// instead of retrying. Physical offsets are independent of sequence counters so
// uint32_t wrap also works for capacities that are not powers of two.
static inline uint32_t spscAudioQueueOccupancy(const SpscAudioQueue* queue) {
    uint32_t read = atomic_load_explicit(&queue->readCounter, memory_order_acquire);
    uint32_t write = atomic_load_explicit(&queue->writeCounter, memory_order_acquire);
    uint32_t count = write - read;
    return count < queue->capacityFrames ? count : queue->capacityFrames;
}

#endif
