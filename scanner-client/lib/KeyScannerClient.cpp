#include "KeyScannerClient.h"

#include <iostream>
#include <string>
#include <unistd.h>

using namespace std;

static void printPorts(RtMidi& midi)
{
    for (unsigned int n = 0; n < midi.getPortCount(); ++n) {
        std::cout << "[" << n << "]" << " " << midi.getPortName(n) << "\n";
    }
}

static int findPortFromName(RtMidi& midi, const std::string& name)
{
    for (unsigned int n = 0; n < midi.getPortCount(); ++n) {
        if (!(midi.getPortName(n).compare(name))) {
            return n;
        }
    }
    return -1;
}

bool ScannerBarClient::open(int inDeviceNum, int outDeviceNum, int midiInPollMs) {
    std::cout << "Input ports available:\n";
    printPorts(in_);
    std::cout << "Output ports available:\n";
    printPorts(out_);
    try {
        if(-1 == outDeviceNum)
            outDeviceNum = findPortFromName(out_, "KeyScanner");
        out_.openPort(outDeviceNum);
        if(-1 == inDeviceNum)
            inDeviceNum = findPortFromName(in_, "KeyScanner");
        this->midiInPollMs = midiInPollMs;
        if(!midiInPollMs) {
            // otherwise start polling thread in start()
            in_.setCallback(rtMidiStaticCallback, this);
        }
        in_.setErrorCallback(rtMidiErrorStaticCallback, this);
        in_.openPort(inDeviceNum);
        in_.ignoreTypes(false, false, false);
        cout << "Scanner MIDI Input Device: (" << inDeviceNum << ": " << in_.getPortName(inDeviceNum) << ")\n";
        cout << in_.getPortName(inDeviceNum) << endl;
    } catch (RtMidiError& err) {
        cerr << "Error opening ScannerBar: ";
        err.printMessage();
        return false;
    }
    return true;
}

bool ScannerBarClient::through(int ms, int deviceNum)
{
    try {
        throughRateMs = ms;
        if(-1 == deviceNum)
            deviceNum = findPortFromName(through_, "IAC Driver Bus 1");
        through_.openPort(deviceNum);
        throughLog.resize(4);
        cout << "Scanner MIDI Through Device: (" << deviceNum << ": " << through_.getPortName(deviceNum) << ")\n";
        return true;
    } catch (RtMidiError& err) {
        cerr << "Error opening Through: ";
        err.printMessage();
        return false;
    }
}

void ScannerBarClient::rtMidiErrorCallback(RtMidiError::Type type, const std::string &errorText) {
    fprintf(stderr, "===========%s\n", errorText.c_str());
}

bool ScannerBarClient::start(int8_t ms, bool watchdog) {
    initialScanInterval = getScanInterval();
    if(ms > 0)
    {
        // Reset the internal calibration of the scanner since we do our own
        if (!resetCalibration())
            return false;
        if (!setTransmissionOptions(ms))
            return false;
        restoreTransmissionOptions = false;
        cachedScanInterval = ms;
    } else {
        cachedScanInterval = initialScanInterval;
        restoreTransmissionOptions = false;
    }
    threadsShouldStop = false;
    if(midiInPollMs > 0)
    {
        midiInPollThread = std::thread(&ScannerBarClient::midiInPollLoop, this);
    }
    if(watchdog)
    {
        watchdogCoeff = 2;
        // if polling, wait longer to accommodate jitter
        if(midiInPollMs > 0)
            watchdogCoeff = midiInPollMs * 2;
        watchdogCounter = 0;
        watchdogStarted = 0;
        watchdogThread = std::thread(&ScannerBarClient::watchdogLoop, this);
    }
    return true;
}

void* ScannerBarClient::watchdogLoop()
{
    while(!threadsShouldStop)
    {
        ++watchdogCounter;
        usleep(watchdogCoeff * cachedScanInterval * 1000);
        if(watchdogCounter > midiUpdated)
        {
            if(!watchdogStarted)
            {
                watchdogStarted = watchdogCounter;
                watchdogStartCallback();
            }
        } else {
            if(watchdogStarted)
            {
                watchdogEndCallback(watchdogCoeff * (watchdogCounter - watchdogStarted));
                watchdogStarted = 0;
            }
        }
    }
    return 0;
}

void ScannerBarClient::inputPoll()
{
    // static to avoid repeated allocations. Only one thread should call this
    static std::vector<unsigned char> msg;
    while(1) {
        double deltaTime = in_.getMessage(&msg);
        if(!msg.size())
            break;
        inputDataCallback(deltaTime, msg);
    }
}

#include <sched.h>
#include <pthread.h>

void* ScannerBarClient::midiInPollLoop()
{
    sched_param sch;
    int policy = SCHED_FIFO;
    sch.sched_priority = sched_get_priority_max(policy);
    int ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sch);
    if(ret)
        fprintf(stderr, "Error while setschedparam: %d %s\n", ret, strerror(ret));
    while(!threadsShouldStop)
    {
        inputPoll();
        usleep(midiInPollMs * 1000);
    }
    return 0;
}

bool ScannerBarClient::stop() {
    threadsShouldStop = true;
    if(watchdogThread.joinable())
    {
        watchdogThread.join();
    }
    if(midiInPollThread.joinable())
    {
        midiInPollThread.join();
    }
    // go back to a more relaxed scanning rate
    if(restoreTransmissionOptions) {
        printf("initial interval: %d\n", initialScanInterval);
        restoreTransmissionOptions = false;
        if (!setTransmissionOptions(initialScanInterval))
            return false;
    }
    return true;
}

void ScannerBarClient::close() {
    stop();
    in_.closePort();
}

static uint64_t unpackU(uint8_t* buffer, size_t len) {
    uint64_t out = 0;
    for (unsigned int n = 0; n < len; ++n)
        out |= (buffer[n] & 0x7F) << (7 * n);
    return out;
}
// 5x 7-bit values --> 1x 32-bit U32
static uint32_t sysexUnpackU32(uint8_t* buffer) { return (uint32_t)unpackU(buffer, 5); }

// 2x 7-bit values centred around 8192 --> 1x 14-bit
static int16_t midiSysexUnpackS14(uint8_t* buffer) {
    int16_t out = unpackU(buffer, 2);
    out -= 8192;
    return out;
}

int16_t ScannerBarClient::getKeyReading(uint8_t* buffer, unsigned int key)
{
    return midiSysexUnpackS14(buffer + key * 2);
}

int ScannerBarClient::parseKeyFrame(uint8_t* frame, size_t length, bool calibrated) {
    midiUpdated = watchdogCounter;
    static int count = 0;
    count++;
    bool verbose_;
    if ((count % 200) == 0)
        verbose_ = 0;
    else
        verbose_ = 0;
    if (length < 5)
        return -1;
    unsigned int octave = (*frame++);
    --length;
    uint32_t ts = sysexUnpackU32(frame);
    frame += 5;
    length -= 5;
    // TODO: the timestamp on the scanner will wrap around every 49 days. Handle that case and then test it.
    processKeyFrame(octave, ts, frame, length, calibrated);
    return octave;
}

#include "scanner_midi.h"

int8_t ScannerBarClient::getScanInterval() {
    sysexGetReady = false;
    uint8_t data[] = {kMidiSysexCommandGet, kMidiSysexCommandTransmissionOptions};
    sendSysex({ data, data + sizeof(data) });
    unsigned int count = 100;
    while(!sysexGetReady && --count)
    {
        usleep(1000);
    }
    if(sysexGetReady)
    {
        if(5 == sysexGetBuffer.size() && kMidiSysexCommandTransmissionOptions == sysexGetBuffer[0])
            return sysexGetBuffer[2];
    }
    return -1;
}

bool ScannerBarClient::sendSysex(const std::vector<uint8_t>& content) {
    std::vector<uint8_t> msg(content.size() + 5);
    unsigned char* u = msg.data();
    *u++ = kMidiMessageSysex;
    *u++ = MIDI_SYSEX_MANUFACTURER;
    *u++ = MIDI_SYSEX_DEVICE;
    *u++ = MIDI_SYSEX_PROTOCOL_VERSION;
    memcpy(u, content.data(), content.size());
    msg.back() = kMidiMessageSysexEnd;
    try {
        out_.sendMessage(&msg);
        return true;
    } catch (std::exception e) {
        std::cerr << e.what() << "\n";
        return false;
    }
}

bool ScannerBarClient::setTransmissionOptions(uint8_t ms) {
    // for whatever reason, building with xcode doesn't accept sendSysex({a, b, c});
    // Yes, I did set it to a -std=c++11, but it doesn't want to know about it.
    uint8_t scanByte = ms > 0 ? kMidiSysexCommandStartScanning : kMidiSysexCommandStopScanning;
    if (!sendSysex({ &scanByte, &scanByte + 1 }))
        return false;
    uint8_t data[] = { kMidiSysexCommandTransmissionOptions, 0, ms, ms, 0 };
    return sendSysex({ data, data + sizeof(data) });
}

bool ScannerBarClient::resetCalibration() {
    uint8_t data[] = { kMidiSysexCommandResetCalibration };
    return sendSysex({ data, data + sizeof(data) });
}

void ScannerBarClient::inputDataCallback(double deltaTime, vector<unsigned char>& msg) {
    if (msg.size() < 5)
        return;
    unsigned char* u = msg.data();
    unsigned char* uEnd = u + msg.size() - 1;
    if (kMidiMessageSysex != *u++ || MIDI_SYSEX_MANUFACTURER != *u++ || MIDI_SYSEX_DEVICE != *u++
        || MIDI_SYSEX_PROTOCOL_VERSION != *u++ || kMidiMessageSysexEnd != msg.back())
    {
        printf("malformed: %d %d %d %d\n", msg[0], msg[1], msg[2], msg[3]);
        return; // not for us or malformed
    }
    int octave = -1;
    uint8_t msgId = *u++;
    switch (msgId) {
    case kMidiSysexCommandAcknowledge:
        break;
    case kMidiSysexCommandErrorMessage:
        printf("ERROR via sysex: %.*s\n", (int)(uEnd - u), u);
        break;
    case kMidiSysexCommandTransmissionOptions:
        sysexGetBuffer.assign(u - 1, uEnd);
        printf("GET: ");
        for(unsigned int n = 0; n < sysexGetBuffer.size(); ++n)
            printf("%d ", sysexGetBuffer[n]);
        printf("\n");
        sysexGetReady = true;
        break;
    case kMidiSysexCommandRawKeySensorData:
    case kMidiSysexCommandCalibratedPositionData:
        octave = parseKeyFrame(u, uEnd - u, kMidiSysexCommandCalibratedPositionData == msgId);
        break;
    default:
        printf("Unhandled sysex msgId: %d\n", msgId);
        printf("[%zu] ", msg.size());
        for (size_t n = 0; n < msg.size(); ++n)
            printf("%3d ", msg[n]);
        printf("\n");
    }
    if(through_.isPortOpen()) {
        bool shouldThrough = true;
        if(octave >= 0)
        {
            // throttle sending position data
            unsigned int idx = octave / 2;
            if(throughLog.size() < idx)
                throughLog.resize(idx);
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<float> diff = now - throughLog[idx];
            if(diff.count() * 1000.f > throughRateMs)
            {
                throughLog[idx] = now;
            } else {
                shouldThrough = false;
            }
        }
        if(shouldThrough)
            through_.sendMessage(&msg);
    }
};
