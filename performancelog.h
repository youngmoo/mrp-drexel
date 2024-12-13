//
//  performancelog.h
//  mrp
//
//  Created by Andrew McPherson on 21/11/2012.
//
//

#ifndef PERFORMANCELOG_H
#define PERFORMANCELOG_H

#include "audiorender.h"
#include "realtimenote.h"
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <vector>

class PerformanceLog {
    friend void* playbackRunLoop(void* data);

public:
    // ************ Constructor *******************
    PerformanceLog(OscController* controller);

    // ************ Recording and Playback *********************
    int startRecording(string& filename); // Enable logging to a file at the given location.
    void pauseRecording(); // Leave file open but pause the recording
    void resumeRecording(); // Resume recording after pause
    void stopRecording(); // Disable logging and close any open file

    bool recording() { return recording_; } // Check whether we're recording or not

    // Start playing back a log file. Also able to transpose everything up/down
    // (in semitones), stretch/compress the time, and set whether to loop the
    // recording.
    int startPlayback(string& filename, int transpose, float timeStretch, bool loop);
    void pausePlayback() { playbackPaused_ = true; }
    void resumePlayback() { playbackPaused_ = false; }
    void stopPlayback();

    // ************* Data Input *******************
    void logMidiMessage(vector<unsigned char>* message); // Log a MIDI messag

    // Log an OSC message. Data following types should be int or float only,
    // matching what's in types
    void logOscMessage(const char* path, const char* types, ...);
    void logOscMessage(const char* path, int value1, int value2, vector<double> doubleValues);

    // ************* Destructor *******************
    ~PerformanceLog();

private:
    // **************** Private Methods **************
    void restartPlayback(); // Having already started the playback, restart it

    unsigned long long currentTime(); // Current wall (system) time in microseconds
    double recordingRelativeTime(); // Current time relative to start of
                                    // recording, in seconds
    double playbackGetNextTimestamp(); // Get the next timestamp in the playback file

    static void* playbackRunLoop(void* data); // Function that runs in its own thread to time the playback

    bool recording_,
        playing_; // Whether we are either writing a log file or reading one
    bool recordingPaused_,
        playbackPaused_; // Whether recording/playback are paused
    bool loopPlayback_; // Whether to loop at the end of the file

    unsigned long long recordingPauseTimestamp_; // When the recording was paused,
                                                 // if it is currently
    unsigned long long recordingOffsetTimestamp_; // When the recording started, to calculate
                                                  // relative timestamps
    unsigned long long playbackStartTime_; // System timestamp when playback started
    double playbackTimestampOffset_; // Offset needed to get from playback file to
                                     // current time
    double lastEventTimestamp_; // Timestamp of the last event we received
    ofstream recordingFile_; // File reference we're recording to
    ifstream playbackFile_; // File reference we're playing from

    pthread_t playbackThread_; // Thread that runs the playback

    OscController* oscController_; // Reference to the OSC controller, needed for playback
};

#endif /* PERFORMANCELOG_H */