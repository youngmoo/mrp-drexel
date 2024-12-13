#pragma once
#include "../../rtmidi/RtMidi.h"
#include <vector>
#include <stdint.h>
#include <thread>
#include <chrono>

class ScannerBarClient
{
public:
    ScannerBarClient() :
        in_(RtMidi::UNSPECIFIED, "KeyScanner input client", 2000)
    {};
    /// @param midiInPollMs -1 for no callback (user has to
    //call inputPoll() manually), 0 for running using rtmidi callback,
    //> 0 for polling interval in ms
    bool open(int inDeviceNum = -1, int outDeviceNum = -1, int midiInPollMs = 0);
    bool start(int8_t ms = -1, bool watchdog = false);
    bool stop();
    void close();
    bool through(int ms, int outDeviceNum = -1);
    bool sendSysex(const std::vector<uint8_t>& content);
    int8_t getScanInterval();
    bool setTransmissionOptions(uint8_t ms);
    bool resetCalibration();
    void inputPoll();
protected:
    int8_t cachedScanInterval = -1;
    static int16_t getKeyReading(uint8_t* buffer, unsigned int key);
    virtual void watchdogStartCallback() {};
    virtual void watchdogEndCallback(unsigned int frames) {};
private:
    void inputDataCallback(double deltaTime, std::vector<unsigned char>& message);
    static void rtMidiStaticCallback(double deltaTime, std::vector<unsigned char>* message, void* userData) {
        auto that = (ScannerBarClient*)userData;
        that->inputDataCallback(deltaTime, *message);
    }
    virtual void rtMidiErrorCallback(RtMidiError::Type type, const std::string &errorText);
    static void rtMidiErrorStaticCallback(RtMidiError::Type type, const std::string &errorText, void *userData) {
        auto that = (ScannerBarClient*)userData;
        that->rtMidiErrorCallback(type, errorText);
    };
    int parseKeyFrame(uint8_t* frame, size_t length, bool calibrated);
    RtMidiIn in_;
    RtMidiOut out_;
    RtMidiOut through_;
    virtual void processKeyFrame(unsigned int octave, uint32_t ts, uint8_t* frame, size_t length, bool calibrated) = 0;
    bool restoreTransmissionOptions = false;
    int8_t initialScanInterval = -1;
    std::vector<uint8_t> sysexGetBuffer;
    volatile bool sysexGetReady = false;
    void* watchdogLoop();
    std::thread watchdogThread;
    volatile uint32_t midiUpdated;
    uint32_t watchdogCoeff;
    uint32_t watchdogCounter;
    uint32_t watchdogStarted;
    volatile bool threadsShouldStop;
    int midiInPollMs;
    void* midiInPollLoop();
    std::thread midiInPollThread;
    int throughRateMs;
    std::vector<std::chrono::time_point<std::chrono::steady_clock>> throughLog;
};
