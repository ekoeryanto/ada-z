#include "sample_store.h"
#include <vector>
#include <Preferences.h>

static int g_totalSensors = 0;
static int g_capacity = 0;

struct SampleEntry {
    int raw;
    float smoothed;
    float volt;
};

// Per-sensor circular buffers and state
static std::vector<std::vector<SampleEntry>> buffers;
static std::vector<int> writeIndex;
static std::vector<int> filledCount;

static const char* PREF_NS = "sstore";

// Helper: preference keys
static String sbufKey(int idx) { char b[32]; snprintf(b, sizeof(b), "sbuf_%d", idx); return String(b); }
static String swiKey(int idx) { char b[32]; snprintf(b, sizeof(b), "swi_%d", idx); return String(b); }
static String scntKey(int idx) { char b[32]; snprintf(b, sizeof(b), "scnt_%d", idx); return String(b); }

// Persist a single sensor buffer into Preferences
static void persistSensor(int idx) {
    Preferences p;
    p.begin(PREF_NS, false);
    size_t bytes = sizeof(SampleEntry) * g_capacity;
    p.putBytes(sbufKey(idx).c_str(), buffers[idx].data(), bytes);
    p.putInt(swiKey(idx).c_str(), writeIndex[idx]);
    p.putInt(scntKey(idx).c_str(), filledCount[idx]);
    p.end();
}

// Load a single sensor buffer from Preferences if present
static void loadSensor(int idx) {
    Preferences p;
    p.begin(PREF_NS, false);
    size_t expected = sizeof(SampleEntry) * g_capacity;
    size_t have = p.getBytesLength(sbufKey(idx).c_str());
    if (have == expected) {
        p.getBytes(sbufKey(idx).c_str(), buffers[idx].data(), expected);
        writeIndex[idx] = p.getInt(swiKey(idx).c_str(), 0);
        filledCount[idx] = p.getInt(scntKey(idx).c_str(), g_capacity);
    } else {
        // mark empty
        writeIndex[idx] = 0;
        filledCount[idx] = 0;
        for (int j = 0; j < g_capacity; ++j) buffers[idx][j].raw = INT_MIN;
    }
    p.end();
}

void initSampleStore(int totalSensors, int samplesPerSensor) {
    g_totalSensors = totalSensors;
    g_capacity = samplesPerSensor;
    buffers.clear();
    writeIndex.clear();
    filledCount.clear();
    buffers.resize(totalSensors);
    writeIndex.resize(totalSensors);
    filledCount.resize(totalSensors);
    for (int i = 0; i < totalSensors; ++i) {
        buffers[i].resize(samplesPerSensor);
        // attempt to load persisted buffer
        loadSensor(i);
    }
}

// Resize per-sensor sample capacity at runtime; preserve as many recent samples as possible
void resizeSampleStore(int samplesPerSensor) {
    if (samplesPerSensor <= 0) return;
    // If capacity unchanged, nothing to do
    if (samplesPerSensor == g_capacity) return;
    // Create new buffers and copy recent data
    std::vector<std::vector<SampleEntry>> newBuffers;
    newBuffers.resize(g_totalSensors);
    for (int i = 0; i < g_totalSensors; ++i) {
        newBuffers[i].resize(samplesPerSensor);
        int copyCount = min(filledCount[i], samplesPerSensor);
        // start index of most recent samples in old buffer
        int oldCap = g_capacity;
        int start = (writeIndex[i] - copyCount + oldCap) % oldCap;
        for (int j = 0; j < copyCount; ++j) {
            int oldIdx = (start + j) % oldCap;
            newBuffers[i][j] = buffers[i][oldIdx];
        }
        // initialize rest
        for (int j = copyCount; j < samplesPerSensor; ++j) newBuffers[i][j].raw = INT_MIN;
    }
    // Swap buffers and update metadata
    buffers.swap(newBuffers);
    g_capacity = samplesPerSensor;
    writeIndex.assign(g_totalSensors, 0);
    for (int i = 0; i < g_totalSensors; ++i) {
        filledCount[i] = min(filledCount[i], samplesPerSensor);
    }
}

void addSample(int sensorIndex, int raw, float smoothed, float volt) {
    if (sensorIndex < 0 || sensorIndex >= g_totalSensors) return;
    int idx = writeIndex[sensorIndex] % g_capacity;
    buffers[sensorIndex][idx].raw = raw;
    buffers[sensorIndex][idx].smoothed = smoothed;
    buffers[sensorIndex][idx].volt = volt;
    writeIndex[sensorIndex] = (writeIndex[sensorIndex] + 1) % g_capacity;
    if (filledCount[sensorIndex] < g_capacity) filledCount[sensorIndex]++;

    // Persist when buffer wraps (i.e., writeIndex returns to 0)
    if (writeIndex[sensorIndex] == 0) {
        persistSensor(sensorIndex);
    }
}

bool getAverages(int sensorIndex, float &avgRaw, float &avgSmoothed, float &avgVolt) {
    if (sensorIndex < 0 || sensorIndex >= g_totalSensors) return false;
    int count = filledCount[sensorIndex];
    if (count == 0) return false;
    long sumRaw = 0;
    double sumSm = 0.0;
    double sumV = 0.0;
    // oldest index = (writeIndex - filledCount + capacity) % capacity
    int cap = g_capacity;
    int start = (writeIndex[sensorIndex] - filledCount[sensorIndex] + cap) % cap;
    for (int i = 0; i < filledCount[sensorIndex]; ++i) {
        int idx = (start + i) % cap;
        sumRaw += buffers[sensorIndex][idx].raw;
        sumSm += buffers[sensorIndex][idx].smoothed;
        sumV += buffers[sensorIndex][idx].volt;
    }
    avgRaw = (float)sumRaw / (float)count;
    avgSmoothed = (float)(sumSm / count);
    avgVolt = (float)(sumV / count);
    return true;
}

int getSampleCount(int sensorIndex) {
    if (sensorIndex < 0 || sensorIndex >= g_totalSensors) return 0;
    return filledCount[sensorIndex];
}

int getSampleCapacity() {
    return g_capacity;
}

void deinitSampleStore() {
    // Persist all buffers on deinit
    for (int i = 0; i < g_totalSensors; ++i) persistSensor(i);
    buffers.clear();
    writeIndex.clear();
    filledCount.clear();
    g_totalSensors = 0;
    g_capacity = 0;
}
