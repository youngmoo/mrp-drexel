#include "RtMidi.h"
#include "audiorender.h"
#include "config.h"
#include "lo/lo.h"
#include "midicontroller.h"
#include "osccontroller.h"
#include "performancelog.h"
#include "pianobar.h"
#include "pitchtrack.h"
#include "synth.h"
#include "wavetables.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <libgen.h>
#include <pthread.h>
#include <set>

using namespace std;

#define DEFAULT_NUM_INPUTS 1
#define DEFAULT_NUM_OUTPUTS 2
#define DEFAULT_BUFFER_SIZE 64
#define DEFAULT_SAMPLE_RATE 44100.
#define DEFAULT_PIANO_BAR_BUFFER_SIZE 32
#define DEFAULT_SCANNER_BAR_POLL -1

#define DEFAULT_OSC_RECEIVE_PORT "7770"
#define DEFAULT_OSC_TRANSMIT_PORT "7777"
#define DEFAULT_OSC_TRANSMIT_HOST "127.0.0.1"
#define DEFAULT_OSC_THRU_HOST "127.0.0.1"
#define DEFAULT_OSC_THRU_PORT "7760"
#define DEFAULT_OSC_PREFIX "/mrp"
#define DEFAULT_TUNING 440.0
#define DEFAULT_BASS_STRETCH 1.005
#define DEFAULT_TREBLE_STRETCH 1.0
#define DEFAULT_STRETCH_SPLIT_POINT 60

#define MRP_USB_CONTROLLER_NAME "MRP Controller"
#define DEFAULT_CONTROLLER_FIRST_STRING 21 // MIDI # of lowest amplifier by default

enum {
    kOptionOscTransmitHost = 1000,
    kOptionOscTransmitPort,
    kOptionOscPrefix,
    kOptionOscTransmitDisable,
    kOptionOscThruPrefix,
    kOptionOscThruHost,
    kOptionOscThruPort,
    kOptionPrioritizeOldNotes,
    kOptionPianoBarMidiChannel,
    kOptionTuning,
    kOptionStretchBass,
    kOptionStretchTreble,
    kOptionStretchSplitPoint,
    kOptionControllerFirstString,
    kOptionControllerDirectionDown,
    kOptionControllerSkipChannels,
    kOptionLogRecord,
    kOptionLogPlayback,
    kOptionLogPlaybackLoop,
    kOptionLogPlaybackTranspose,
    kOptionLogPlaybackTimeStretch,
    kOptionLogStartPaused
};

static struct option long_options[] = {
    { "help", no_argument, NULL, 'h' },
    { "list", no_argument, NULL, 'l' },
    { "list-long", no_argument, NULL, 'L' },
    { "input-device", required_argument, NULL, 'i' },
    { "output-device", required_argument, NULL, 'o' },
    { "input-channels", required_argument, NULL, 'I' },
    { "output-channels", required_argument, NULL, 'O' },
    { "output-channel-list", required_argument, NULL, 'a' },
    { "output-channel-downmix", required_argument, NULL, 'D' },
    { "audio-buffer", required_argument, NULL, 'b' },
    { "sample-rate", required_argument, NULL, 's' },
    { "midi-inputs", required_argument, NULL, 'M' },
    { "midi-input-disable", no_argument, NULL, 'm' },
    { "disable-midi-notes", required_argument, NULL, 'n' },
    { "midi-output", required_argument, NULL, 'Q' },
    { "disable-midi-output", no_argument, NULL, 'q' },
    { "patch-file", required_argument, NULL, 'p' },
    { "calibration-file", required_argument, NULL, 'c' },
    { "pb-device", required_argument, NULL, 'P' },
    { "pb-buffer", required_argument, NULL, 'B' },
    { "sb-device-in", required_argument, NULL, 'S' },
    { "sb-device-out", required_argument, NULL, 'T' },
    { "sb-poll", required_argument, NULL, 'U' },
    { "pb-cal", required_argument, NULL, 'C' },
    { "pb-midi-channel", required_argument, NULL, kOptionPianoBarMidiChannel },
    { "osc-receive-port", required_argument, NULL, 'Z' },
    { "osc-receive-disable", no_argument, NULL, 'z' },
    { "osc-transmit-host", required_argument, NULL, kOptionOscTransmitHost },
    { "osc-transmit-port", required_argument, NULL, kOptionOscTransmitPort },
    { "osc-transmit-disable", no_argument, NULL, kOptionOscTransmitDisable },
    { "osc-path-prefix", required_argument, NULL, kOptionOscPrefix },
    { "osc-thru-prefix", required_argument, NULL, kOptionOscThruPrefix },
    { "osc-thru-host", required_argument, NULL, kOptionOscThruHost },
    { "osc-thru-port", required_argument, NULL, kOptionOscThruPort },
    { "prioritize-old-notes", no_argument, NULL, kOptionPrioritizeOldNotes },
    { "tuning", required_argument, NULL, kOptionTuning },
    { "stretch-bass", required_argument, NULL, kOptionStretchBass },
    { "stretch-treble", required_argument, NULL, kOptionStretchTreble },
    { "stretch-split", required_argument, NULL, kOptionStretchSplitPoint },
    { "controller-first-string", required_argument, NULL, kOptionControllerFirstString },
    { "controller-direction-down", no_argument, NULL, kOptionControllerDirectionDown },
    { "controller-skip-channels", required_argument, NULL, kOptionControllerSkipChannels },
    { "log-record", required_argument, NULL, kOptionLogRecord },
    { "log-playback", required_argument, NULL, kOptionLogPlayback },
    { "log-playback-loop", no_argument, NULL, kOptionLogPlaybackLoop },
    { "log-playback-transpose", required_argument, NULL, kOptionLogPlaybackTranspose },
    { "log-playback-timestretch", required_argument, NULL, kOptionLogPlaybackTimeStretch },
    { "log-start-paused", no_argument, NULL, kOptionLogStartPaused },
    { 0, 0, 0, 0 }
};

// Define this globally since lots of different processes use it.  Not
// programmatically clean, but saves a lot of code.
WaveTable waveTable;

void usage(const char* processName) // Print usage information and exit
{
    cerr << "Usage: " << processName
         << " [-h] [-l] [-i #] [-o #] [-I #] [-O #] [-b #] [-s #] [-p file.xml] "
            "[-c file.txt] [-Z port]\n";
    cerr << "  -h:   Print this menu\n";
    cerr << "  -l:   List available audio and MIDI devices (short form)\n";
    cerr << "  -L:   List available audio and MIDI devices (long form)\n";
    cerr << "  -i #: Number of the input device to use (default: system "
            "default)\n";
    cerr << "  -o #: Number of the output device to use (default: system "
            "default)\n";
    cerr << "  -I #: Number of input channels (default: " << DEFAULT_NUM_INPUTS << ")\n";
    cerr << "  -O #: Number of output channels (default: " << DEFAULT_NUM_OUTPUTS << ")\n";
    cerr << "  -a <list>: List of output channels to use (default: all)\n";
    cerr << "  -D <list>: List of downmix channels to use. To be used when -O is larger than the number of outputs on "
            "the device (default: none)\n";
    cerr << "  -b #: Buffer size (default: " << DEFAULT_BUFFER_SIZE << ")\n";
    cerr << "  -s #: Sample rate (default: " << (int)DEFAULT_SAMPLE_RATE << ")\n";
    cerr << "  -M #,#,...: MIDI input devices to use (default: all, "
            "sequentially)\n";
    cerr << "              First device in list taken to be main keyboard; "
            "others are aux keyboards\n";
    cerr << "  -m:   Disable MIDI input\n";
    cerr << "  -n <list>: Disable MIDI note triggering on indicated channels\n";
    cerr << "  -Q #: Number of MIDI output device to use (default: USB MRP "
            "controller only)\n";
    cerr << "  -q:   Disable MIDI output\n";
    cerr << "  -p file.xml: Read patch info from file (default: mrp.xml)\n";
    cerr << "  -c file.txt: Read calibration data from file (default: "
            "mrp-calibration.txt)\n";
    cerr << "  -Z port: use the given port for the OSC server [default: 7770]\n";
    cerr << "  -z: disable OSC\n";
    cerr << "  -P #: Number of audio input device for Piano Bar data\n";
    cerr << "  -B #: Buffer size for Piano Bar (default: " << DEFAULT_PIANO_BAR_BUFFER_SIZE << ")\n";
    cerr << "  -S #: Number of midi input device for Scanner Bar data (default: auto(-1))\n";
    cerr << "  -T #: Number of midi output device for Scanner Bar data (default: auto(-1))\n";
    cerr << "  -U #: Scanner data processing behaviour: -1 data is processed from audio thread, 0 data is processed "
            "from MIDI thread, > 0 argument is number of ms of polling thread (default: "
         << DEFAULT_SCANNER_BAR_POLL << ")\n";
    cerr << "  -R #: Number of midi through device for throttled scanner bar data (default: none(-1))\n";
    cerr << "  -C file.txt: Read Piano Bar calibration from file (default: "
            "mrp-pb-calibration.txt)\n";
    cerr << "  --tuning <freq>: Set the frequency of A4 (default: " << DEFAULT_TUNING << ")\n";
    cerr << "  --stretch-bass <ratio>: Set the stretch ratio for bass strings "
            "(default: "
         << DEFAULT_BASS_STRETCH << ")\n";
    cerr << "  --stretch-treble <ratio>: Set the stretch ratio for treble "
            "strings (default: "
         << DEFAULT_TREBLE_STRETCH << ")\n";
    cerr << "  --stretch-split <note>: Set the note where the bass/treble split "
            "changes (default: "
         << DEFAULT_STRETCH_SPLIT_POINT << ")\n";
    cerr << "  --osc-path-prefix: specify the prefix to all OSC paths (default: " << DEFAULT_OSC_PREFIX << ")\n";
    cerr << "  --osc-transmit-host: host to which to transmit OSC messages "
            "(default: "
         << DEFAULT_OSC_TRANSMIT_HOST << ")\n";
    cerr << "  --osc-transmit-port: port to which to transmit OSC messages "
            "(default: "
         << DEFAULT_OSC_TRANSMIT_PORT << ")\n";
    cerr << "  --osc-transmit-disable: disable OSC message transmission\n";
    cerr << "  --osc-thru-prefix: prefix of messages to be passed through to "
            "another host (default: disabled)\n";
    cerr << "  --osc-thru-host: host to transmit thru messages to (default: " << DEFAULT_OSC_THRU_HOST << ")\n";
    cerr << "  --osc-thru-port: port to transmit thru messages to (default: " << DEFAULT_OSC_THRU_PORT << ")\n";
    cerr << "  --pb-midi-channel <ch>: set the MIDI channel the PianoBar sends "
            "to (0-15, default: 15)\n";
    cerr << "  --prioritize-old-notes: continue sounding the earliest notes if "
            "out of channels (default: turn off earliest notes)\n";
    cerr << "  --controller-first-string <string>: MIDI note of first amplifier "
            "on MRP controller (default: "
         << DEFAULT_CONTROLLER_FIRST_STRING << ")\n";
    cerr << "  --controller-direction-down: MRP amplifiers are in descending "
            "order (default: ascending)\n";
    cerr << "  --controller-skip-channels <a,b,c>: List of MRP controller "
            "channels to skip\n";
    cerr << "  --log-record <filename>: record the performance to the indicated "
            "file\n";
    cerr << "  --log-playback <filename>: play back a performance stored in the "
            "indicated filed\n";
    cerr << "  --log-playback-loop: loop the playback of the given file\n";
    cerr << "  --log-playback-transpose <semitones>: transpose the playback by a "
            "given amount\n";
    cerr << "  --log-playback-timestretch <stretch>: stretch the timing of the "
            "playback by a given factor (< 1 slower, > 1 faster)\n";
    cerr << "  --log-start-paused: start the recording or playback in the paused "
            "state\n";

    exit(0);
}

static void PrintSupportedStandardSampleRates(const PaStreamParameters* inputParameters,
                                              const PaStreamParameters* outputParameters) {
    static double standardSampleRates[] = {
        8000.0,  9600.0,  11025.0, 12000.0, 16000.0, 22050.0,  24000.0,
        32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, -1 /* negative terminated  list */
    };
    int i, printCount;
    PaError err;

    printCount = 0;
    for (i = 0; standardSampleRates[i] > 0; i++) {
        err = Pa_IsFormatSupported(inputParameters, outputParameters, standardSampleRates[i]);
        if (err == paFormatIsSupported) {
            if (printCount == 0) {
                printf("      %8.2f", standardSampleRates[i]);
                printCount = 1;
            } else if (printCount == 5) {
                printf(",\n      %8.2f", standardSampleRates[i]);
                printCount = 1;
            } else {
                printf(", %8.2f", standardSampleRates[i]);
                ++printCount;
            }
        }
    }
    if (!printCount)
        printf("None\n");
    else
        printf("\n");
}

void exit_with_error(PaError err) // Called when an unrecoverable error occurs
{
    Pa_Terminate();
    fprintf(stderr, "An error occured while using the portaudio stream\n");
    fprintf(stderr, "Error number: %d\n", err);
    fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    exit(err);
}

void osc_error_handler(int num, const char* msg, const char* path) {
    fprintf(stderr, "liblo server error %d in path %s: %s\n", num, path, msg);
    fflush(stderr);
}

void list_devices(bool details) // Show available input and output devices, then exit
{
    int i, numDevices, defaultDisplayed;
    const PaDeviceInfo* deviceInfo;
    PaStreamParameters inputParameters, outputParameters;
    PaError err;

    Pa_Initialize();

    cout << Pa_GetVersionText() << " (version " << Pa_GetVersion() << ")\n";

    numDevices = Pa_GetDeviceCount();
    if (numDevices < 0) {
        printf("ERROR: Pa_GetDeviceCount returned 0x%x\n", numDevices);
        err = numDevices;
        exit_with_error(err);
    }

    cout << "----------------------- AUDIO DEVICES -----------------------\n";

    for (i = 0; i < numDevices; i++) {
        deviceInfo = Pa_GetDeviceInfo(i);

        cout << "Device " << i << ": " << deviceInfo->name << " (";
        cout << Pa_GetHostApiInfo(deviceInfo->hostApi)->name << ") ";

        /* Mark global and API specific default devices */
        defaultDisplayed = 0;
        if (i == Pa_GetDefaultInputDevice()) {
            cout << "[ Default Input";
            defaultDisplayed = 1;
        }
        if (i == Pa_GetDefaultOutputDevice()) {
            cout << (defaultDisplayed ? "," : "[") << " Default Output";
            defaultDisplayed = 1;
        }
        if (defaultDisplayed)
            cout << " ]";
        cout << endl;

        cout << fixed;
        cout << "    max inputs: " << deviceInfo->maxInputChannels;
        cout << ", max outputs: " << deviceInfo->maxOutputChannels << endl;
        if (details) {
            cout << "    input latency: " << setprecision(3) << deviceInfo->defaultLowInputLatency;
            cout << ", output latency: " << setprecision(3) << deviceInfo->defaultLowOutputLatency << endl;
            cout << "    default sample rate: " << setprecision(1) << deviceInfo->defaultSampleRate << endl;

            /* poll for standard sample rates */
            inputParameters.device = i;
            inputParameters.channelCount = deviceInfo->maxInputChannels;
            inputParameters.sampleFormat = paFloat32;
            inputParameters.suggestedLatency = 0; /* ignored by Pa_IsFormatSupported() */

            // If we're on Mac, make sure we set the hardware to the actual sample
            // rate and don't rely on SRC
            /*if(deviceInfo->hostApi == paCoreAudio || deviceInfo->hostApi == 0)
            // FIXME: kludge for portaudio bug?
            {
                    PaMacCoreStreamInfo macInfo;

                    PaMacCore_SetupStreamInfo(&macInfo,
            paMacCoreChangeDeviceParameters | paMacCoreFailIfConversionRequired);
                    inputParameters.hostApiSpecificStreamInfo = &macInfo;
            }
            else*/
            inputParameters.hostApiSpecificStreamInfo = NULL;

            outputParameters.device = i;
            outputParameters.channelCount = deviceInfo->maxOutputChannels;
            outputParameters.sampleFormat = paFloat32;
            outputParameters.suggestedLatency = 0; /* ignored by Pa_IsFormatSupported() */

            /*if(deviceInfo->hostApi == paCoreAudio || deviceInfo->hostApi == 0)
            {
                    PaMacCoreStreamInfo macInfo;

                    PaMacCore_SetupStreamInfo(&macInfo,
            paMacCoreChangeDeviceParameters | paMacCoreFailIfConversionRequired);
                    outputParameters.hostApiSpecificStreamInfo = &macInfo;
            }
            else*/
            outputParameters.hostApiSpecificStreamInfo = NULL;

            if (inputParameters.channelCount > 0) {
                cout << "    input sample rates:\n";
                PrintSupportedStandardSampleRates(&inputParameters, NULL);
            }
            if (outputParameters.channelCount > 0) {
                cout << "    output sample rates:\n";
                PrintSupportedStandardSampleRates(NULL, &outputParameters);
            }
            if (inputParameters.channelCount > 0 && outputParameters.channelCount > 0) {
                cout << "    full-duplex sample rates:\n";
                PrintSupportedStandardSampleRates(&inputParameters, &outputParameters);
            }
        }
    }

    Pa_Terminate();

    cout << "----------------------- MIDI DEVICES ------------------------\n";

    try {
        RtMidiIn* midiIn;
        RtMidiOut* midiOut;
        int count;

        midiIn = new RtMidiIn();
        count = midiIn->getPortCount();

        if (count == 0)
            cout << "No MIDI input devices.\n";
        for (i = 0; i < count; i++) {
            cout << "Input Device " << i << ": " << midiIn->getPortName(i) << endl;
        }

        delete midiIn;

        midiOut = new RtMidiOut();
        count = midiOut->getPortCount();

        if (count == 0)
            cout << "No MIDI output devices.\n";
        for (i = 0; i < count; i++) {
            cout << "Output Device " << i << ": " << midiOut->getPortName(i) << endl;
        }

        delete midiOut;
    } catch (RtMidiError& err) {
        cout << "Error getting MIDI devices: ";
        err.printMessage();
        exit(EXIT_FAILURE);
    }

    exit(0);
}

// These actions defined for the console input, so an empty line will continue
// to increment/decrement the program number if that's what happened before.

enum { ACTION_NONE = 0, ACTION_INCREMENT, ACTION_DECREMENT };

static void scannerBarControllerPollCallback(void* arg) {
    // could have used a lambda, but we are trying to keep compatibility with ancient C++
    if (arg) {
        ScannerBarController* sb = (ScannerBarController*)arg;
        sb->inputPoll();
    }
}

int main(int argc, char* const argv[]) {
    // streams are broken on some build configurations on macos, e.g.:
    // libstdc++ with SDK 10.9, so we perform a runtime check.
    {
        std::stringstream ss;
        int a = 1234;
        ss << a;
        if (ss.fail() || ss.str() != std::string("1234")) {
            std::cout << "streams are broken on your machine and/or build configuration\n";
            return 1;
        }
    }

    // ---- Audio ----
    AudioRender* mainRender = new AudioRender();
    PaDeviceIndex inputDeviceNum = paNoDevice, outputDeviceNum = paNoDevice;
    PaStreamParameters outputParameters, inputParameters, *inputPointer;
    PaStream* stream;
    const PaDeviceInfo* deviceInfo;
    int numStreamOutputChannels;
    int numInputChannels = DEFAULT_NUM_INPUTS, numOutputChannels = DEFAULT_NUM_OUTPUTS;
    int bufferSize = DEFAULT_BUFFER_SIZE;
    float sampleRate = DEFAULT_SAMPLE_RATE;
    float tuning = DEFAULT_TUNING;
    float bassStretch = DEFAULT_BASS_STRETCH, trebleStretch = DEFAULT_TREBLE_STRETCH;
    int stretchSplitPoint = DEFAULT_STRETCH_SPLIT_POINT;
    vector<int> audioChannels;
    vector<int> downmixChannels;
    PaError err;

    // ---- MIDI ----
    MidiController* mainMidiController = new MidiController(mainRender);
    RtMidiOut* midiOut;
    bool useMidiIn = true, useMidiOut = true;
    int midiOutputNum = -1;
    vector<unsigned int> midiInputNums;
    vector<MidiController::midiCallbackStruct*> midiCallbackStructs;
    bool displaceOldNotes = true;
    vector<int> midiDisabledChannels;
    string *patchTableFile = NULL, *calibrationTableFile = NULL, *pianoBarCalibrationTableFile = NULL;

    // ---- New MRP Controller ----
    int usbControllerFirstString = DEFAULT_CONTROLLER_FIRST_STRING;
    bool usbControllerDirectionDown = false;
    vector<int> controllerSkipChannels;

    // ---- OSC ----
    OscController* oscController = NULL;
    char *oscReceivePort = NULL, *oscTransmitHost = NULL, *oscTransmitPort = NULL, *oscPathPrefix = NULL;
    char *oscThruPort = NULL, *oscThruHost = NULL, *oscThruPrefix = NULL;
    bool useOsc = true, useOscMidi = true, useOscTransmit = true, useOscThru = false;
    lo_server_thread oscServerThread = NULL;
    lo_address oscTransmitAddress = NULL, oscThruAddress = NULL;

    // ---- PitchTrack (legacy, needs updating) ----
    PitchTrackController* pitchTrackController = NULL;

    // ---- ContinousController  -----
    ContinuousKeyController* pianoBarController = NULL;
    PaDeviceIndex pianoBarDeviceNum = paNoDevice;
    int pianoBarBufferSize = DEFAULT_PIANO_BAR_BUFFER_SIZE;
    int scannerBarInDeviceNum = -1;
    int scannerBarOutDeviceNum = -1;
    int pianoBarMidiChannel = -1;
    int scannerBarPoll = DEFAULT_SCANNER_BAR_POLL;
    int scannerBarThrough = -1;

    // ---- Logging -----
    PerformanceLog* logController;
    string *logRecordFile = 0, *logPlaybackFile = 0;
    bool logPlaybackLoop = false;
    float logPlaybackTimeStretch = 1.0;
    int logPlaybackTranspose = 0;
    bool logStartPaused = false;

    // ---- Other variables ----
    int ch, i, option_index;
    timedParameter tp;

    while (
        (ch = getopt_long(argc, argv, "hlLmqzi:o:I:O:b:s:M:Q:p:c:Z:a:D:P:B:S:T:U:R:C:n:", long_options, &option_index))
        != -1) {
        char *str, *ap;
        unsigned int thisInputNum;
        bool duplicate;

        switch (ch) {
        case 'l':
            list_devices(false);
            break;
        case 'L':
            list_devices(true);
            break;
        case 'i':
            inputDeviceNum = atoi(optarg);
            break;
        case 'o':
            outputDeviceNum = atoi(optarg);
            break;
        case 'I':
            numInputChannels = atoi(optarg);
            break;
        case 'O':
            numOutputChannels = atoi(optarg);
            break;
        case 'b':
            bufferSize = atoi(optarg);
            break;
        case 's':
            sampleRate = atof(optarg);
            break;
        case 'M':
            str = strdup(optarg);
            // Parse the argument as a comma-separated list
            while ((ap = strsep(&str, ", \t")) != NULL) {
                thisInputNum = atoi(ap);
                duplicate = false; // Make sure we don't already have this MIDI input
                for (i = 0; i < midiInputNums.size(); i++)
                    if (midiInputNums[i] == thisInputNum)
                        duplicate = true;
                if (!duplicate) {
                    midiInputNums.push_back(thisInputNum);
                    // cout << "Adding input " << thisInputNum;
                }
            }
            free(str);
            break;
        case 'Q':
            midiOutputNum = atoi(optarg);
            break;
        case 'm':
            useMidiIn = false;
            break;
        case 'q':
            useMidiOut = false;
            break;
        case 'p':
            patchTableFile = new string(optarg);
            break;
        case 'c':
            calibrationTableFile = new string(optarg);
            break;
        case 'Z':
            oscReceivePort = strdup(optarg);
            break;
        case 'z':
            useOsc = false;
            useOscMidi = false;
            break;
        case 'a':
            audioChannels = MidiController::parseRangeString(optarg);
            break;
        case 'D':
            downmixChannels = MidiController::parseRangeString(optarg);
            break;
        case 'P':
            pianoBarDeviceNum = atoi(optarg);
            break;
        case 'B':
            pianoBarBufferSize = atoi(optarg);
            break;
        case 'S':
            scannerBarInDeviceNum = atoi(optarg);
            break;
        case 'T':
            scannerBarOutDeviceNum = atoi(optarg);
            break;
        case 'U':
            scannerBarPoll = atoi(optarg);
            break;
        case 'R':
            scannerBarThrough = atoi(optarg);
            break;
        case 'C':
            pianoBarCalibrationTableFile = new string(optarg);
            break;
        case 'n':
            midiDisabledChannels = MidiController::parseRangeString(optarg);
            break;
        case kOptionOscTransmitHost:
            oscTransmitHost = strdup(optarg);
            break;
        case kOptionOscTransmitPort:
            oscTransmitPort = strdup(optarg);
            break;
        case kOptionOscTransmitDisable:
            useOscTransmit = false;
            break;
        case kOptionOscPrefix:
            oscPathPrefix = strdup(optarg);
            break;
        case kOptionPrioritizeOldNotes:
            displaceOldNotes = false;
            break;
        case kOptionOscThruPrefix:
            oscThruPrefix = strdup(optarg);
            useOscThru = true;
            break;
        case kOptionOscThruHost:
            oscThruHost = strdup(optarg);
            break;
        case kOptionOscThruPort:
            oscThruPort = strdup(optarg);
            break;
        case kOptionTuning:
            tuning = atof(optarg);
            break;
        case kOptionStretchBass:
            bassStretch = atof(optarg);
            break;
        case kOptionStretchTreble:
            trebleStretch = atof(optarg);
            break;
        case kOptionStretchSplitPoint:
            stretchSplitPoint = atoi(optarg);
            break;
        case kOptionPianoBarMidiChannel:
            pianoBarMidiChannel = atoi(optarg);
            break;
        case kOptionControllerFirstString:
            usbControllerFirstString = atoi(optarg);
            if (usbControllerFirstString < 0)
                usbControllerFirstString = 0;
            if (usbControllerFirstString > 127)
                usbControllerFirstString = 127;
            break;
        case kOptionControllerDirectionDown:
            usbControllerDirectionDown = true;
            break;
        case kOptionControllerSkipChannels:
            controllerSkipChannels = MidiController::parseRangeString(optarg);
            break;
        case kOptionLogRecord:
            logRecordFile = new string(optarg);
            break;
        case kOptionLogPlayback:
            logPlaybackFile = new string(optarg);
            break;
        case kOptionLogPlaybackLoop:
            logPlaybackLoop = true;
            break;
        case kOptionLogPlaybackTranspose:
            logPlaybackTranspose = atoi(optarg);
            if (logPlaybackTranspose < -96)
                logPlaybackTranspose = -96;
            if (logPlaybackTranspose > 96)
                logPlaybackTranspose = 96;
            break;
        case kOptionLogPlaybackTimeStretch:
            logPlaybackTimeStretch = atof(optarg);
            if (logPlaybackTimeStretch < 0.01)
                logPlaybackTimeStretch = 0.01;
            if (logPlaybackTimeStretch > 100.0)
                logPlaybackTimeStretch = 100.0;
            break;
        case kOptionLogStartPaused:
            logStartPaused = true;
            break;
        case 'h': // Print help screen
        case '?':
        default:
            usage(basename(argv[0]));
        }
    }
    if (patchTableFile == NULL)
        patchTableFile = new string("mrp.xml");
    if (calibrationTableFile == NULL)
        calibrationTableFile = new string("mrp-calibration.txt");
    if (pianoBarCalibrationTableFile == NULL)
        pianoBarCalibrationTableFile = new string("mrp-pb-calibration.txt");
    if (oscTransmitHost == NULL && useOscTransmit)
        oscTransmitHost = strdup(DEFAULT_OSC_TRANSMIT_HOST);
    if (oscTransmitPort == NULL && useOscTransmit)
        oscTransmitPort = strdup(DEFAULT_OSC_TRANSMIT_PORT);
    if (oscPathPrefix == NULL && (useOsc || useOscTransmit))
        oscPathPrefix = strdup(DEFAULT_OSC_PREFIX);
    if (oscReceivePort == NULL && useOsc)
        oscReceivePort = strdup(DEFAULT_OSC_RECEIVE_PORT);
    if (oscThruHost == NULL && useOscThru)
        oscThruHost = strdup(DEFAULT_OSC_THRU_HOST);
    if (oscThruPort == NULL && useOscThru)
        oscThruPort = strdup(DEFAULT_OSC_THRU_PORT);

    // ************************** AUDIO **********************************

    numStreamOutputChannels = numOutputChannels;
    // Initialize portaudio
    err = Pa_Initialize();
    if (err != paNoError)
        exit_with_error(err);

    if (outputDeviceNum == paNoDevice) // Get default output, if not otherwise specified
        outputDeviceNum = Pa_GetDefaultOutputDevice();
    if (outputDeviceNum == paNoDevice) // If there's still no output, generate an error.
    {
        cerr << "Error: no default output device. Try -l for list of devices.\n";
        Pa_Terminate();
        return 1;
    } else // Check that this device supports as many channels as we want
    {
        deviceInfo = Pa_GetDeviceInfo(outputDeviceNum);
        if (deviceInfo == NULL) {
            cerr << "Invalid output device " << outputDeviceNum << ".  Try -l for list of devices.\n";
            Pa_Terminate();
            return 1;
        }
        cout << "Audio Output Device: " << deviceInfo->name << endl;

        if (deviceInfo->maxOutputChannels <= 0) {
            cerr << "Error: device does not support output.  Try -l for list of "
                    "devices.\n";
            Pa_Terminate();
            return 1;
        }
        if (numOutputChannels > deviceInfo->maxOutputChannels) {
            numStreamOutputChannels = deviceInfo->maxOutputChannels;
            if (0 == downmixChannels.size()) // if no downmixing
                numOutputChannels = numStreamOutputChannels; // clamp the max num of channels
            cerr << "Warning: output device only supports " << numOutputChannels << " channels.\n";
        }
    }

    if (inputDeviceNum == paNoDevice) // Get default input, if not otherwise specified
        inputDeviceNum = Pa_GetDefaultInputDevice();
    if (inputDeviceNum == paNoDevice) // Can run without input
    {
        cerr << "Warning: no default input device, input disabled.  Try -l for "
                "list of devices.\n";
        numInputChannels = 0;
    } else // Check that this device supports as many channels as we want
    {
        deviceInfo = Pa_GetDeviceInfo(inputDeviceNum);
        if (deviceInfo == NULL) {
            cerr << "Invalid input device " << inputDeviceNum << ".  Try -l for list of devices.\n";
            Pa_Terminate();
            return 1;
        }
        cout << "Audio Input Device:  " << deviceInfo->name << endl;

        if (deviceInfo->maxInputChannels <= 0) {
            cerr << "Warning: device does not support input.  Input is disabled.\n";
            numInputChannels = 0;
        }
        if (numInputChannels > deviceInfo->maxInputChannels) {
            numInputChannels = deviceInfo->maxInputChannels;
            cerr << "Warning: input device only supports " << numInputChannels << " channels.\n";
        }
    }

    cout << numOutputChannels << " output channels, " << numInputChannels << " input channels, ";
    cout << sampleRate / 1000. << "kHz sample rate, " << bufferSize << " frames per buffer\n";

    outputParameters.device = outputDeviceNum;
    outputParameters.channelCount = numStreamOutputChannels;
    outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;

    // If we're on Mac, make sure we set the hardware to the actual sample rate
    // and don't rely on SRC
    if (deviceInfo->hostApi == paCoreAudio || deviceInfo->hostApi == 0) // FIXME: kludge for portaudio bug?
    {
        PaMacCoreStreamInfo macInfo;

        PaMacCore_SetupStreamInfo(&macInfo, paMacCoreChangeDeviceParameters | paMacCoreFailIfConversionRequired);
        outputParameters.hostApiSpecificStreamInfo = &macInfo;
    } else
        outputParameters.hostApiSpecificStreamInfo = NULL;

    inputParameters.device = inputDeviceNum;
    inputParameters.channelCount = numInputChannels;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;

    // If we're on Mac, make sure we set the hardware to the actual sample rate
    // and don't rely on SRC
    if (deviceInfo->hostApi == paCoreAudio || deviceInfo->hostApi == 0) // FIXME: kludge for portaudio bug?
    {
        PaMacCoreStreamInfo macInfo;

        PaMacCore_SetupStreamInfo(&macInfo, paMacCoreChangeDeviceParameters | paMacCoreFailIfConversionRequired);
        inputParameters.hostApiSpecificStreamInfo = &macInfo;
    } else
        inputParameters.hostApiSpecificStreamInfo = NULL;

    // If we don't use input, pass NULL as the input parameters
    inputPointer = &inputParameters;
    if (numInputChannels <= 0)
        inputPointer = NULL;

    // Check whether these parameters will work with the given sample rate
    err = Pa_IsFormatSupported(inputPointer, &outputParameters, sampleRate);
    if (err != paFormatIsSupported) {
        cerr << "Error: Sample rate " << sampleRate << " not supported with given devices and channels.\n";
        Pa_Terminate();
        return err;
    }

    // Open the stream, passing the mainRender object as user data--
    // staticRenderCallback() uses this to call the object-specific
    // renderCallback() function.
    err = Pa_OpenStream(&stream, inputPointer, &outputParameters, sampleRate, bufferSize, paNoFlag,
                        AudioRender::staticRenderCallback, mainRender);
    if (err != paNoError)
        exit_with_error(err);

    // Set up our output object which handles the render callbacks, passing it the
    // stream to keep track of.  IMPORTANT: This has to be done before any XML
    // parsing, since this will tell us the sampleRate and other important
    // parameters that everything uses.

    mainRender->setStreamInfo(stream, numInputChannels, numOutputChannels, numStreamOutputChannels, sampleRate,
                              audioChannels, downmixChannels);

    // If we have channels to skip, set them up here
    if (!controllerSkipChannels.empty())
        mainRender->setMRPControllerChannelsToSkip(controllerSkipChannels);

    // ******************************** MIDI **********************************

    mainMidiController->setA4Tuning(tuning, bassStretch, trebleStretch, stretchSplitPoint);

    // Initialize the MIDI inputs and outputs
    if (useMidiOut) {
        try {
            midiOut = new RtMidiOut;

            if (midiOutputNum < 0) {
                int i;

                // Look for dedicated MIDI controller
                for (i = 0; i < midiOut->getPortCount(); i++) {
                    if (!(midiOut->getPortName(i).compare(MRP_USB_CONTROLLER_NAME))) {
                        midiOutputNum = i;
                        midiOut->openPort(midiOutputNum); // Try to open the port

                        mainMidiController->setMidiOutputDevice(midiOut);
                        mainMidiController->mrpSetMidiChannel(0); // Always channel 0 for now
                        mainMidiController->mrpSetBaseAndDirection(usbControllerFirstString, usbControllerDirectionDown,
                                                                   127);
                        break;
                    }
                }

                if (i == midiOut->getPortCount()) {
                    cerr << "Unable to find MRP controller. Try -l to check connected "
                            "devices.\n";
                    delete midiOut;
                    exit(1);
                }
            } else {
                midiOut->openPort(midiOutputNum); // Try to open the port

                mainMidiController->setMidiOutputDevice(midiOut);
                mainMidiController->mrpSetMidiChannel(0); // Always channel 0 for now
                mainMidiController->mrpSetBaseAndDirection(0, false, 0);
            }

            cout << "MIDI Output Device: " << midiOut->getPortName(midiOutputNum) << endl;
        } catch (RtMidiError& err) {
            RtMidiError::Type errType = err.getType();

            if (errType == RtMidiError::WARNING || errType == RtMidiError::DEBUG_WARNING) {
                // Non-critical error, print but go on
                cerr << "MIDI output warning: ";
                err.printMessage();
            } else {
                // The most common error will be that we can't open that port
                if (errType == RtMidiError::INVALID_DEVICE)
                    cerr << "Invalid MIDI output device " << midiOutputNum << ". Try -l for list of devices.\n";
                else {
                    cerr << "MIDI output error: ";
                    err.printMessage();
                }
                cerr << "MIDI output disabled.\n";

                useMidiOut = false;
                delete midiOut;
            }
        }
    } else
        cout << "MIDI output disabled\n";

    if (useMidiIn || useOscMidi) {
        RtMidiIn* midiIn;
        MidiController::midiCallbackStruct* midiStruct;
        int actualInput;

        mainMidiController->setDisplaceOldNotes(displaceOldNotes);

        if (useMidiIn) {
            if (midiInputNums.size() == 0) {
                // If useMidiIn is set, but no objects are in midiInputNums, the default
                // behavior is to use all available MIDI inputs, ordered sequentially.

                try {
                    midiIn = new RtMidiIn;
                    int numDevices = midiIn->getPortCount();

                    for (i = 0; i < numDevices; i++)
                        midiInputNums.push_back(i);
                    delete midiIn;
                } catch (RtMidiError err) {
                    cerr << "MIDI input error: ";
                    err.printMessage();
                    delete midiIn;
                    midiInputNums.clear(); // Disable MIDI input
                }
            }

            for (i = 0, actualInput = 0; i < midiInputNums.size(); i++) {
                try {
                    midiStruct = NULL;
                    midiIn = new RtMidiIn;
                    midiIn->openPort(midiInputNums[i]); // Try to open the port
                    midiIn->ignoreTypes(true, true,
                                        true); // Ignore sysex, timing, and active sensing messages

                    if (actualInput == 0)
                        cout << "Main MIDI Input Device: ";
                    else
                        cout << "Aux. MIDI Input Device: ";
                    cout << midiIn->getPortName(midiInputNums[i]) << endl;

                    midiStruct = new MidiController::midiCallbackStruct;
                    midiStruct->controller = mainMidiController;
                    midiStruct->midiIn = midiIn;
                    midiStruct->inputNumber = actualInput;
                    midiIn->setCallback(MidiController::rtMidiStaticCallback, midiStruct);
                    midiCallbackStructs.push_back(midiStruct);

                    actualInput++; // The first successful device becomes our main
                                   // keyboard, all others auxiliary
                } catch (RtMidiError& err) {
                    RtMidiError::Type errType = err.getType();

                    if (errType == RtMidiError::WARNING || errType == RtMidiError::DEBUG_WARNING) {
                        // Non-critical error, print but go on
                        cerr << "MIDI input warning: ";
                        err.printMessage();
                    } else {
                        // The most common error will be that we can't open that port
                        if (errType == RtMidiError::INVALID_DEVICE)
                            cerr << "Invalid MIDI input device " << midiInputNums[i]
                                 << ". Try -l for list of devices.\n";
                        else {
                            cerr << "MIDI input error: ";
                            err.printMessage();
                        }
                        delete midiIn;
                        if (midiStruct)
                            delete midiStruct;
                    }
                }
            }
        }

        if (midiCallbackStructs.size() == 0 && !useOscMidi) {
            // If no devices have been added, disable MIDI input entirely
            cerr << "MIDI input disabled.\n";

            useMidiIn = false;
        } else {
            if (midiCallbackStructs.size() == 0)
                cout << "No physical MIDI ports found; using OSC emulation only.\n";

            // Tell the controller which MIDI channels to ignore for note purposes
            mainMidiController->setNoteDisabledChannels(midiDisabledChannels);
        }
    } else
        cout << "MIDI input disabled\n";

    // *************************** PIANO BAR (legacy)
    // **********************************

    // Initialize Piano Bar input device if relevant
    const float historyInSeconds = 0.5;
    if (pianoBarDeviceNum != paNoDevice) {
        auto pb = new PianoBarController(mainMidiController);
        pb->open(pianoBarDeviceNum, pianoBarBufferSize, historyInSeconds);
        pianoBarController = pb;
    }
    if (!pianoBarController) {
        auto sb = new ScannerBarController(mainMidiController);
        sb->open(scannerBarInDeviceNum, scannerBarOutDeviceNum, historyInSeconds, 0.001, scannerBarPoll);
        pianoBarController = sb;
        if (scannerBarPoll < 0) {
            mainRender->setPollCallback(scannerBarControllerPollCallback, (void*)sb);
        }
        if (scannerBarThrough >= 0)
            sb->through(100, scannerBarThrough);
    }
    if (pianoBarController && pianoBarMidiChannel >= 0 && pianoBarMidiChannel < 16)
        pianoBarController->setMidiChannel(pianoBarMidiChannel);

    // ******************************** OSC
    // ***************************************

    // Initialize OSC server, if enabled
    if (useOsc) {
        cout << "Initializing OSC server on port " << oscReceivePort << endl;
        oscServerThread = lo_server_thread_new(oscReceivePort, osc_error_handler); // Create new thread

        if (oscServerThread == NULL) {
            cerr << "Error initializing OSC server.  OSC disabled.\n";
            useOsc = false;
            // continuing now would result in segfault later.
            // TODO: properly cleanup already instantiated objects and/or properly handle
            // useOsc so that no segfault happens.
            return 1;
        } else {
            // Create an object to handle all the OSC messages
            oscTransmitAddress = lo_address_new(oscTransmitHost, oscTransmitPort);
            oscController = new OscController(oscServerThread, oscTransmitAddress,
                                              oscPathPrefix); // Create the controller

            oscController->setMidiController(mainMidiController); // Pass a reference to the MIDI controller
            oscController->setUseOscMidi(useOscMidi);
            mainMidiController->setOscController(oscController);
            mainRender->setOscController(oscController);

            // Set the parameters for transmitting messages to other hosts
            if (useOscThru) {
                oscThruAddress = lo_address_new(oscThruHost, oscThruPort);
                oscController->setThruAddress(oscThruAddress, oscThruPrefix);
            }

            // The Pitch Track controller operates separately...

            pitchTrackController = new PitchTrackController(mainMidiController);
            pitchTrackController->setOscController(oscController);
            // lo_server_thread_add_method(oscServerThread, "/mrp/ptrk", NULL,
            // PitchTrackController::staticOscHandler,
            // (void *)pitchTrackController);

            lo_server_thread_start(oscServerThread);
        }
    } else
        cout << "OSC server disabled\n";

    // Load patch/program info from file
    if (mainMidiController->loadPatchTable(*patchTableFile) != 0) {
        cerr << "Error reading patch table info from '" << *patchTableFile << "'\n";
        Pa_Terminate();
        exit(1);
    }
    // Load calibration data from file
    if (mainMidiController->loadCalibrationTable(*calibrationTableFile) != 0) {
        cerr << "Warning: error reading calibration info from '" << *calibrationTableFile << "'\n";
        mainMidiController->clearCalibration();
    }
    // Load Piano Bar calibration data from file
    if (pianoBarController != NULL) {
        if (!pianoBarController->loadCalibrationFromFile(*pianoBarCalibrationTableFile)) {
            cerr << "Warning: error reading Piano Bar calibration info from '" << *pianoBarCalibrationTableFile
                 << "'\n";
        }
    }

    err = Pa_StartStream(stream); // Start the audio stream
    if (err != paNoError)
        exit_with_error(err);

    if (pianoBarController != NULL)
        if (!pianoBarController->start())
            cout << "Warning: error starting Piano Bar controller\n";

    // **************************** Logging ********************************
    logController = new PerformanceLog(oscController);

    mainMidiController->setLogController(logController);
    if (pianoBarController != 0)
        pianoBarController->setLogController(logController);

    if (logRecordFile != 0) {
        if (logController->startRecording(*logRecordFile)) {
            cerr << "Unable to start logging on file " << *logRecordFile << endl;
            Pa_Terminate();
            exit(1);
        }

        if (logStartPaused)
            logController->pauseRecording();
        delete logRecordFile;
    }

    if (logPlaybackFile != 0) {
        if (logController->startPlayback(*logPlaybackFile, logPlaybackTranspose, logPlaybackTimeStretch,
                                         logPlaybackLoop)) {
            cerr << "Unable to play file " << *logPlaybackFile << endl;
            Pa_Terminate();
            exit(1);
        }

        if (logStartPaused)
            logController->pausePlayback();
        delete logPlaybackFile;
    }

    // *************************** Main Run Loop ***************************

    bool shouldStop = false;
    int currentAction = ACTION_NONE;
    char inputLine[256];
    string inputString, buf;
    vector<string> tokenizedString;

    while (!shouldStop) {
        cin.getline(inputLine, 256); // Read a command from stdin
        if (!cin.good()) {
            // if we are here, it is most likely because we are running in a debugger or profiler, which may send
            // spurious signals
            cin.clear();
            usleep(10000);
            continue;
        }
        inputString = inputLine;
        tokenizedString.clear();

        // Tokenize the input string by whitespace
        stringstream ss(inputString);
        while (ss >> buf)
            tokenizedString.push_back(buf);

        if (tokenizedString.size() == 0) // Blank line entered
        {
            switch (currentAction) {
            case ACTION_INCREMENT:
                cout << "Program incremented to " << mainMidiController->consoleProgramIncrement() << endl;
                continue;
            case ACTION_DECREMENT:
                cout << "Program decremented to " << mainMidiController->consoleProgramDecrement() << endl;
                continue;
            case ACTION_NONE: // Ignore, wait for next line
            default:
                continue;
            }
        }

        currentAction = ACTION_NONE;

        // Execute commands
        if (tokenizedString[0] == "q" || tokenizedString[0] == "quit")
            shouldStop = true;
        else if (tokenizedString[0] == "c" || tokenizedString[0] == "cpu") {
            cout << "CPU Load: " << mainRender->cpuLoad() << endl;
        } else if (tokenizedString[0] == "l" || tokenizedString[0] == "load") {
            string fileName;
            if (tokenizedString.size() >= 2)
                fileName = tokenizedString[1];
            else
                fileName = *patchTableFile; // Use default if no file specified

            if (mainMidiController->loadPatchTable(fileName) != 0) {
                cerr << "Error reading patch table info from '" << fileName << "'\n";
                Pa_Terminate();
                exit(1);
            }
            cout << "Loaded patch table\n";
        } else if (tokenizedString[0] == "lc" || tokenizedString[0] == "loadcal") {
            string fileName;
            if (tokenizedString.size() >= 2)
                fileName = tokenizedString[1];
            else
                fileName = *calibrationTableFile; // Use default if no file specified

            if (mainMidiController->loadCalibrationTable(fileName) != 0) {
                cerr << "Error reading calibration table info from '" << fileName << "'. Calibration cleared\n";
                mainMidiController->clearCalibration();
            } else
                cout << "Loaded calibration table\n";
        } else if (tokenizedString[0] == "cc" || tokenizedString[0] == "clearcal") {
            mainMidiController->clearCalibration();
            cout << "Calibration data cleared\n";
        } else if (tokenizedString[0] == "sc" || tokenizedString[0] == "savecal") {
            string fileName;
            if (tokenizedString.size() >= 2)
                fileName = tokenizedString[1];
            else
                fileName = *calibrationTableFile; // Use default if no file specified

            if (mainMidiController->saveCalibrationTable(fileName) != 0) {
                cerr << "Error saving calibration table info to '" << fileName << "'.\n";
            } else
                cout << "Saved calibration table\n";
        } else if (tokenizedString[0] == "p" || tokenizedString[0] == "program") {
            if (tokenizedString.size() < 2)
                cout << "Usage: program [id#]\n";
            else {
                // stringstream s(tokenizedString[1]);
                int pr = atoi(tokenizedString[1].c_str());

                // s >> pr;		// Get the program ID number
                if (pr >= 0 && pr < 128) {
                    mainMidiController->consoleProgramChange(pr);
                } else {
                    cout << "Program ID should be 0 to 127\n";
                }
            }
        } else if (tokenizedString[0] == "pp") {
            // Increment the program by one
            cout << "Program incremented to " << mainMidiController->consoleProgramIncrement() << endl;
            currentAction = ACTION_INCREMENT;
        } else if (tokenizedString[0] == "pd") {
            // Decrement the program by one
            cout << "Program decremented to " << mainMidiController->consoleProgramDecrement() << endl;
            currentAction = ACTION_DECREMENT;
        } else if (tokenizedString[0] == "a" || tokenizedString[0] == "allnotesoff") {
            cout << "Sending All Notes Off...\n";
            mainMidiController->consoleAllNotesOff(-1);
            if (pitchTrackController != NULL) {
                pitchTrackController->allNotesOff();
            }
        } else if (tokenizedString[0] == "v" || tokenizedString[0] == "volume") {
            // Set the master output volume (amplitude)
            if (tokenizedString.size() < 2)
                cout << "Usage: volume [vol]\n";
            else {
                // stringstream s(tokenizedString[1]);
                float vol = atof(tokenizedString[1].c_str());

                // s >> vol;
                mainRender->setGlobalAmplitude(vol);

                cout << "Volume set to " << vol << endl;
            }
        } else if (tokenizedString[0] == "pbc" || tokenizedString[0] == "pbcal") {
            vector<int> keysToCalibrate;

            if (tokenizedString.size() >= 2)
                keysToCalibrate = MidiController::parseRangeString(tokenizedString[1]);

            if (pianoBarController != NULL) {
                if (pianoBarController->isCalibrating())
                    pianoBarController->stopCalibration();
                else if (!pianoBarController->startCalibration(keysToCalibrate, false))
                    cout << "Unable to start calibration.  Check that piano bar stream "
                            "is running.\n";
                else {
                    cout << "Calibrating Piano Bar.  Press each key at first softly, "
                            "then with heavy pressure (same key press).\n";
                    cout << "Run 'pbc' again to finish calibration.\n";
                }
            } else
                cout << "Error: Piano Bar not enabled.\n";
        } else if (tokenizedString[0] == "pbi" || tokenizedString[0] == "pbidle") {
            vector<int> keysToCalibrate;

            if (tokenizedString.size() >= 2)
                keysToCalibrate = MidiController::parseRangeString(tokenizedString[1]);

            if (pianoBarController != NULL) {
                if (!pianoBarController->startCalibration(keysToCalibrate, true))
                    cout << "Unable to start calibration.  Check that piano bar stream "
                            "is running.\n";
            } else
                cout << "Error: Piano Bar not enabled.\n";
        } else if (tokenizedString[0] == "pblc" || tokenizedString[0] == "pbloadcal") {
            string fileName;
            if (tokenizedString.size() >= 2)
                fileName = tokenizedString[1];
            else
                fileName = *pianoBarCalibrationTableFile; // Use default if no file specified

            if (!pianoBarController->loadCalibrationFromFile(fileName)) {
                cerr << "Error reading Piano Bar calibration info from '" << fileName << "'. Calibration cleared\n";
            } else
                cout << "Loaded Piano Bar calibration info\n";
        } else if (tokenizedString[0] == "pbsc" || tokenizedString[0] == "pbsavecal") {
            string fileName;
            if (tokenizedString.size() >= 2)
                fileName = tokenizedString[1];
            else
                fileName = *pianoBarCalibrationTableFile; // Use default if no file specified

            if (!pianoBarController->saveCalibrationToFile(fileName)) {
                cerr << "Error saving Piano Bar calibration info to '" << fileName << "'.\n";
            } else
                cout << "Saved Piano Bar calibration info\n";
        } else if (tokenizedString[0] == "pbs" || tokenizedString[0] == "pbstart" || tokenizedString[0] == "pbstop") {
            if (pianoBarController != NULL) {
                if (!pianoBarController->isInitialized())
                    cout << "Error: Piano Bar not initialized.\n";
                else {
                    if (tokenizedString[0] == "pbstart"
                        || (tokenizedString[0] == "pbs" && !pianoBarController->isRunning()))
                        pianoBarController->start();
                    else if (tokenizedString[0] == "pbstop"
                             || (tokenizedString[0] == "pbs" && pianoBarController->isRunning()))
                        pianoBarController->stop();
                }
            } else
                cout << "Error: Piano Bar not enabled.\n";
        } else if (tokenizedString[0] == "pbstatus") {
            if (pianoBarController != NULL) {
                pianoBarController->printKeyStatus();
            } else
                cout << "Error: Piano Bar not enabled.\n";
        } else if ("log" == tokenizedString[0]) {
            if (tokenizedString.size() >= 3) {
                std::string& filename = tokenizedString[1];
                std::vector<int> keys = MidiController::parseRangeString(tokenizedString[2]);
                bool ret = pianoBarController->logOpen(filename, keys);
                if (ret) {
                    cout << "Logging notes ";
                    for (auto& k : keys)
                        cout << k << ", ";
                    cout << "to file " << filename << ". Call `log` again to stop\n";
                } else {
                    cout << "Error opening logging file " << filename
                         << ". File already existing and/or invalid key range?\n";
                }
            } else {
                pianoBarController->logRequestClose();
                cout << "Logged to file\n";
            }
        } else if (tokenizedString[0] == "?" || tokenizedString[0] == "help") {
            cout << "Commands:\n\n";
            cout << "program # [p #]: set the current program number (0-127)\n";
            cout << "pp: increment the current program (subsequent blank lines "
                    "repeat pp)\n";
            cout << "pd: decrement the current program (subsequent blank lines "
                    "repeat pd)\n";
            cout << "volume # [v #]: set the current volume (scaled to 1.0)\n";
            cout << "allnotesoff [a]: turn all notes off\n";
            cout << "load <name> [l <name>]: load patch table from <name> (optional, "
                    "default is given on command line\n";
            cout << "cpu [c]: print current CPU load\n";
            cout << "loadcal <name> [lc <name>]: load actuator calibration from file "
                    "<name> (optional)\n";
            cout << "savecal <name> [sc <name>]: save actuator calibration to <name> "
                    "(optional)\n";
            cout << "clearcal [cc]: clear actuator calibration values\n";
            cout << "pbcal <keys> [pbc <keys>]: start or stop Piano Bar calibration "
                    "(optionally specifying particular keys)\n";
            cout << "pbloadcal <name> [pblc <name>]: load Piano Bar calibration from "
                    "file <name> (optional)\n";
            cout << "pbsavecal <name> [pbsc <name>]: save Piano Bar calibration to "
                    "file <name>\n";
            cout << "pbstatus: Print current Piano Bar key status\n";
            cout << "quit [q]: quit program\n";
            cout << "help [?]: print this message\n";
        }
    }

    cout << "Exiting...\n";

    // ***** End Main Run Loop *****

    // Stop any recording and playback
    logController->stopPlayback();
    logController->stopRecording();

    if (useMidiIn) // Then, stop the MIDI callbacks from generating new notes
    {
        for (i = 0; i < midiCallbackStructs.size(); i++)
            (midiCallbackStructs[i]->midiIn)->cancelCallback();
    }
    err = Pa_StopStream(stream);
    if (err != paNoError)
        exit_with_error(err);

    err = Pa_CloseStream(stream);
    if (err != paNoError)
        exit_with_error(err);

    Pa_Terminate();

    // delete pianoBarController after closing the audio stream,
    // so that if we are processing inputs from the ScannerBar in the
    // audio thread, the latter is stopped before we destroy the ScannerBarController
    if (pianoBarController != NULL) {
        pianoBarController->close();
        delete pianoBarController;
    }

    if (useMidiIn) {
        for (i = 0; i < midiCallbackStructs.size(); i++) {
            delete midiCallbackStructs[i]->midiIn; // Delete the RtMidi object we created earlier
            delete midiCallbackStructs[i]; // Now delete the whole struct
        }
    }
    if (useMidiOut)
        delete midiOut;
    delete patchTableFile;
    delete calibrationTableFile;
    delete pianoBarCalibrationTableFile;
    delete mainMidiController;
    delete mainRender;
    delete logController;
    if (useOsc) {
        delete pitchTrackController;
        lo_server_thread_stop(oscServerThread);
        delete oscController;
        lo_server_thread_free(oscServerThread);
        lo_address_free(oscTransmitAddress);
        if (oscThruAddress != NULL)
            lo_address_free(oscThruAddress);
    }
    if (oscReceivePort != NULL)
        free(oscReceivePort);
    if (oscTransmitPort != NULL)
        free(oscTransmitPort);
    if (oscTransmitHost != NULL)
        free(oscTransmitHost);
    if (oscThruHost != NULL)
        free(oscThruHost);
    if (oscThruPort != NULL)
        free(oscThruPort);
    if (oscThruPrefix != NULL)
        free(oscThruPrefix);
    if (oscPathPrefix != NULL)
        free(oscPathPrefix);
    return 0;
}
