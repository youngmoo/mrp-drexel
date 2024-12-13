//
//  performancelog.cpp
//  mrp
//
//  Created by Andrew McPherson on 21/11/2012.
//
//

#include "performancelog.h"
#include <sys/time.h>

// Constructor: pass reference to OscController for playback
PerformanceLog::PerformanceLog(OscController* controller):
    recording_(false), playing_(false), oscController_(controller), lastEventTimestamp_(0.0) { }

// Destructor: close up any current recordings
PerformanceLog::~PerformanceLog() {
    if (recording_)
        stopRecording();
    if (playing_)
        stopPlayback();
}

// Start recording events to a file
int PerformanceLog::startRecording(string& filename) {
    try {
        recordingFile_.open(filename.c_str(), ios::out);
        if (recordingFile_.fail()) // Failed to open file...
            return 1;

        recordingOffsetTimestamp_ = currentTime();
        recordingPaused_ = false;
        recording_ = true;
    } catch (...) {
        recording_ = false;
        return 1;
    }

    return 0;
}

// Pause a recording in progress
void PerformanceLog::pauseRecording() {
    // Check if already paused -- can't do it twice
    if (recordingPaused_ || !recording_)
        return;

    recordingPauseTimestamp_ = currentTime();
    recordingPaused_ = true;
}

void PerformanceLog::resumeRecording() {
    // Can't resume if not previously paused
    if (!recordingPaused_ || !recording_)
        return;

    // Increase the offset value which means that subsequent events
    // will not reflect the time spent in pause mode
    recordingOffsetTimestamp_ += currentTime() - recordingPauseTimestamp_;
    recordingPaused_ = false;
}

// Stop a currently recording file
void PerformanceLog::stopRecording() {
    if (recording_) {
        recordingFile_ << recordingRelativeTime() << " END" << endl;
        recordingFile_.close();
    }
    recording_ = false;
}

// Start playing events from a file
int PerformanceLog::startPlayback(string& filename, int transpose, float timeStretch, bool loop) {
    cout << "starting playback...";

    try {
        playbackFile_.open(filename.c_str(), ios::in);
        if (playbackFile_.fail()) // Failed to open file...
            return 1;

        playbackPaused_ = false;
        playing_ = true;
        loopPlayback_ = loop;

        playbackStartTime_ = currentTime();
        playbackTimestampOffset_ = playbackGetNextTimestamp();
    } catch (...) {
        playing_ = false;
        return 1;
    }

    // Start thread to read from file
    if (pthread_create(&playbackThread_, NULL, playbackRunLoop, this) != 0) {
        playing_ = false;
        playbackFile_.close();
        return 1;
    }

    cout << "done.\n";

    return 0;
}

// Stop a currently playing file
void PerformanceLog::stopPlayback() {
    cout << "Stopping playback...";

    if (playing_) {
        playing_ = false;

        // Stop the playback thread: will terminate when it finds playing_ has
        // changed
        pthread_join(playbackThread_, NULL);

        playbackFile_.close();
    }

    cout << "done.\n";
}

// Log a MIDI message to the file
void PerformanceLog::logMidiMessage(vector<unsigned char>* message) {
    if (!recording_ || recordingPaused_)
        return;

    if (message->size() == 1) {
        recordingFile_ << recordingRelativeTime() << " /mrp/midi iii " << (int)(*message)[0] << " 0 0" << endl;
    } else if (message->size() == 2) {
        recordingFile_ << recordingRelativeTime() << " /mrp/midi iii " << (int)(*message)[0] << " "
                       << (int)(*message)[1] << " 0" << endl;
    } else if (message->size() >= 3) {
        recordingFile_ << recordingRelativeTime() << " /mrp/midi iii " << (int)(*message)[0];
        recordingFile_ << " " << (int)(*message)[1] << " " << (int)(*message)[2] << endl;
    }
}

// Log an OSC message to the file
void PerformanceLog::logOscMessage(const char* path, const char* types, ...) {
    if (!recording_ || recordingPaused_)
        return;
    if (path == 0 || types == 0)
        return;

    recordingFile_ << recordingRelativeTime() << " " << path << " " << types << " ";

    va_list ap;
    va_start(ap, types);

    for (int i = 0; i < strlen(types); i++) {
        if (types[i] == 'i')
            recordingFile_ << va_arg(ap, int) << " ";
        else if (types[i] == 'f')
            recordingFile_ << va_arg(ap, double) << " ";
    }

    va_end(ap);
    recordingFile_ << endl;
}

// Log an OSC message with specific format containing two ints and a variable
// number of floats. This is a common pattern for note messages.
void PerformanceLog::logOscMessage(const char* path, int value1, int value2, vector<double> doubleValues) {
    if (!recording_ || recordingPaused_)
        return;
    if (path == 0)
        return;

    // Auto-generate the typetag according to the number of elements in the vector
    recordingFile_ << recordingRelativeTime() << " " << path << " ii";
    for (int i = 0; i < doubleValues.size(); i++) {
        recordingFile_ << "f";
    }

    recordingFile_ << " " << value1 << " " << value2;
    for (int i = 0; i < doubleValues.size(); i++) {
        recordingFile_ << " " << (float)doubleValues[i];
    }

    recordingFile_ << endl;
}

// Restart playback currently in progress, from the beginning
void PerformanceLog::restartPlayback() {
    if (!playing_)
        return;

    // Seek to the beginning of the file
    playbackFile_.clear();
    playbackFile_.seekg(0, ios::beg);

    // Recalibrate the initial time
    playbackStartTime_ = currentTime();
    playbackTimestampOffset_ = playbackGetNextTimestamp();
}

// Get the next timestamp in the file (first number on the next line). Returns <
// 0 if an error occurred
double PerformanceLog::playbackGetNextTimestamp() {
    if (!playing_ || playbackFile_.fail() || playbackFile_.eof())
        return -1.0;

    double timestamp;

    // Save the current position, peak ahead one double, then restore the current
    // position
    streampos position = playbackFile_.tellg();
    playbackFile_ >> timestamp;
    playbackFile_.seekg(position);

    return timestamp;
}

unsigned long long PerformanceLog::currentTime() {
    struct timeval tv;

    (void)gettimeofday(&tv, (struct timezone*)NULL);

    return (unsigned long long)tv.tv_sec * 1000000ULL + (unsigned long long)tv.tv_usec;
}

double PerformanceLog::recordingRelativeTime() {
    unsigned long long ct = currentTime();

    return (ct - recordingOffsetTimestamp_) / 1000000.0;
}

// Static function to handle the timing to play back a log file
void* PerformanceLog::playbackRunLoop(void* data) {
    PerformanceLog* logController = (PerformanceLog*)data;
    char lineBuffer[2048]; // Buffer to hold one line of OSC data
    string path = ""; // OSC path
    string typetag; // OSC typetag for each message
    lo_message msg; // OSC message object
    bool messageIsValid;

    while (logController->playing_) {
        if (logController->playbackFile_.eof() || path == "END") {
            // Got to the end of the file: either loop back to beginning or stop
            if (logController->loopPlayback_) {
                cout << "End of playback reached. Looping.\n";

                // Very short pause to limit the rate of reseeking on empty files
                logController->oscController_->allNotesOff();
                usleep(100000);
                logController->restartPlayback();
            } else {
                cout << "End of playback reached. Stopping.\n";

                logController->stopPlayback();
                break;
            }
        }

        if (logController->playbackFile_.fail()) {
            cerr << "Error occurred during file playback; stopping.\n";
            logController->stopPlayback();
            break;
        }

        // Find the current time according to the audio device
        double currentTime = (logController->currentTime() - logController->playbackStartTime_) / 1000000.0;
        double nextEventTime, timeUntilNextEvent;

        // Read one line of the file containing a timestamp and an OSC message
        logController->playbackFile_.getline(lineBuffer, 2048);
        stringstream ss(lineBuffer);

        // Parse the string we just found...
        // First get the next file timestamp, then convert it to our internal clock
        // units
        ss >> nextEventTime;

        // Then get the path and typetag
        ss >> path;

        messageIsValid = false;
        if (path == "SPLICE") {
            // A splice lets us use discontinuous timestamps and resync the offset of
            // the timestamps

            cout << "Splicing: current time is now " << nextEventTime << endl;
            logController->playbackStartTime_ = logController->currentTime();
            logController->playbackTimestampOffset_ = nextEventTime;
            continue;
        } else if (path != "END") {
            nextEventTime -= logController->playbackTimestampOffset_;

            // Display information if something is going to happen in the
            // non-immediate future
            if (nextEventTime - currentTime >= 1.0)
                cout << "Current time is " << currentTime << ", next event is at " << nextEventTime << endl;

            ss >> typetag;

            // Parse the rest of the message and add to lo_message object
            msg = lo_message_new();
            messageIsValid = true;

            for (int i = 0; i < typetag.length(); i++) {
                if (typetag[i] == 'i') {
                    int val;
                    ss >> val;
                    lo_message_add_int32(msg, val);
                } else if (typetag[i] == 'f') {
                    float val;
                    ss >> val;
                    lo_message_add_float(msg, val);
                } else {
                    messageIsValid = false;
                    lo_message_free(msg);
                }
            }
        }

        if (!messageIsValid)
            continue;

        timeUntilNextEvent = nextEventTime - currentTime;
        while (timeUntilNextEvent > 0.2) {
            // Check 5 times a second for getting close (also if we should break)
            usleep(200000);
            if (!logController->playing_)
                return 0;
            currentTime = (logController->currentTime() - logController->playbackStartTime_) / 1000000.0;
            timeUntilNextEvent = nextEventTime - currentTime;

            // cout << "Waiting: currentTime " << currentTime << " time reamining " <<
            // timeUntilNextEvent << endl;
        }

        // cout << "Finished waiting\n";

        // Sleep the rest of the difference between current time and next event
        if ((int)(timeUntilNextEvent * 1000000.0) > 0)
            usleep(timeUntilNextEvent * 1000000.0);

        currentTime = (logController->currentTime() - logController->playbackStartTime_) / 1000000.0;
        // cout << currentTime << ": " << path << " " << typetag << endl;

        logController->oscController_->handler(path.c_str(), typetag.c_str(), lo_message_get_argv(msg),
                                               lo_message_get_argc(msg), msg, 0);
        lo_message_free(msg);
    }

    // End with nothing playing
    logController->oscController_->allNotesOff();

    // cout << "cleaning up playback\n";

    return 0;
}
