#include <KeyScannerClient.h>
#include <stdio.h>
#include <unistd.h>

#include <vector>

class ScannerBarTester : public ScannerBarClient {
    struct Metadata {
        uint32_t ts;
        uint32_t busSeq;
        uint32_t bufSeq;
        uint32_t midiSeq;
    };
public:
    ScannerBarTester(unsigned int histSize) {
        md.resize(4, std::vector<Metadata>(histSize));
        writeIdx.resize(md.size());
        shouldPrint = false;
    };
    void requestPrint()
    {
        shouldPrint = true;
    }
protected:
    virtual void watchdogStartCallback();
    virtual void watchdogEndCallback(unsigned int frames);
private:
    virtual void processKeyFrame(unsigned int octave, uint32_t ts, uint8_t* frame, size_t length, bool calibrated);
    void printState();
    std::vector<std::vector<Metadata>> md;
    std::vector<size_t> writeIdx;
    volatile bool shouldPrint;
};

const unsigned int kMidiSeqKey = 0;
const unsigned int kBusSeqKey = 4;
const unsigned int kBufSeqKey = 5;
const unsigned int kMidiSeqMax = 2048;
const unsigned int kBusSeqMax = 4096;
const unsigned int kBufSeqMax = 1024;

#include <sys/time.h>

char const* getTime()
{
    // https://stackoverflow.com/questions/43732241/how-to-get-datetime-from-gettimeofday-in-c
    const unsigned int SEC_PER_DAY = 86400;
    const unsigned int SEC_PER_HOUR = 3600;
    const unsigned int SEC_PER_MIN = 60;
    struct timeval tp;
    struct timezone tz;
    if(gettimeofday(&tp, &tz))
        return "";
    long hms = tp.tv_sec % SEC_PER_DAY;
    hms += tz.tz_dsttime * SEC_PER_HOUR;
    hms -= tz.tz_minuteswest * SEC_PER_MIN;
    // mod `hms` to insure in positive range of [0...SEC_PER_DAY)
    hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

    // Tear apart hms into h:m:s
    int hour = (int)(hms / SEC_PER_HOUR);
    int min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
    int sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN; // or hms % SEC_PER_MIN
    static char buffer[100];
    snprintf(buffer, 100, "%02d:%02d:%02d.%03d", hour, min, sec, tp.tv_usec / 1000);
    return buffer;
}

void ScannerBarTester::watchdogStartCallback() {
    fprintf(stderr, "%s Scanner bar stopped sending data\n", getTime());
}

void ScannerBarTester::watchdogEndCallback(unsigned int frames) {
    fprintf(stderr, "%s Scanner bar started sending data after dropping approximately %d frames\n", getTime(), frames);
}

void ScannerBarTester::processKeyFrame(unsigned int octave, uint32_t ts, uint8_t* frame, size_t length, bool calibrated)
{
    unsigned int b  = octave / 2;
    if(b >= md.size())
        return;
    auto& v = md[b];
    auto& w = writeIdx[b];
    int oldI = (w - 1 + v.size()) % v.size();
    Metadata& oldM = v[oldI];
    Metadata& m = v[w];
    m.ts = ts;
    m.midiSeq = getKeyReading(frame, kMidiSeqKey);
    m.busSeq = getKeyReading(frame, kBusSeqKey);
    m.bufSeq = getKeyReading(frame, kBufSeqKey);
    ++w;
    if(w >= v.size())
        w = 0;

    if(m.ts - oldM.ts != cachedScanInterval)
        printf("S%d %u %u------ %u\n", b, m.ts, oldM.ts, m.ts - oldM.ts);

    /*
    if(b > 0 && ((m.busSeq - oldM.busSeq + kBusSeqMax) % kBusSeqMax) != cachedScanInterval)
        printf("B%d %u %u %u\n", b, m.ts, m.busSeq, oldM.busSeq);
    if(b > 0 && ((m.bufSeq - oldM.bufSeq + kBufSeqMax) % kBufSeqMax) != cachedScanInterval)
        printf("F%d %u %u %u\n", b, m.ts, m.bufSeq, oldM.bufSeq);
    if(((m.midiSeq - oldM.midiSeq + kMidiSeqMax) % kMidiSeqMax) != 1)
        printf("M%d %u %u %u\n", b, m.ts, m.midiSeq, oldM.midiSeq);
    */
}

void ScannerBarTester::printState()
{
#if 0
    for(unsigned int b = 0; b < tss.size(); ++b)
    {
        auto& v = tss[b];
        const auto w = writeIdx[b];
        std::vector<std::pair<uint32_t, uint32_t>> missing;
        size_t r = (w + 1) % v.size();
        uint32_t oldTs = v[r];
        r++;
        bool inGap = false;
        uint32_t gapStart;
        while(r != w)
        {
            uint32_t ts = v[r];
            if(ts - oldTs != 1)
            {
                if(!inGap)
                {
                    gapStart = oldTs;
                    inGap = true;
                }
            } else {
                if(inGap)
                {
                    inGap = false;
                    uint32_t gapStop = ts;
                    missing.push_back({gapStart, gapStop});
                    printf("S%d %u %u\n", b, gapStop - 1, gapStart);
                }
            }
            oldTs = ts;
            ++r;
            if(r >= v.size())
                r = 0;
        }
    }
#endif
}

#include <signal.h>
int gShouldStop = 0;
void handler(int)
{
    gShouldStop = 1;
}

int main(int argc, char** argv)
{
    ScannerBarTester t(2000);
    int8_t sendInterval = -1;
    int out = -1;
    ++argv;
    --argc;
    int throughDevice = -1;
    int throughMs = 100;
    while(argc)
    {
        int argcStart = argc;
        if(!strcmp("-t", argv[0]))
        {
            throughDevice = atoi(argv[1]);
            argv += 2;
            argc -= 2;
        }
        if(!strcmp("-r", argv[0]))
        {
            throughMs = atoi(argv[1]);
            argv += 2;
            argc -= 2;
        }
        if(argcStart == argc) // no matches
            break;
    }
    if(argc >= 1)
    {
        int val = atoi(argv[0]);
        if(val > 0 && val < 128)
            sendInterval = val;
    }
    t.through(throughMs, throughDevice);
    t.open();
    t.start(sendInterval, 1);
    signal(SIGINT, handler);
    while(!gShouldStop)
    {
        usleep(100000);
    }
    t.stop();
    t.close();
    printf("Exit\n");
    return 0;
}
