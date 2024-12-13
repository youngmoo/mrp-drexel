#pragma once

/* Attributes for MIDI sysex commands */
#define MIDI_SYSEX_MANUFACTURER 0x7D /* Temporary code */
#define MIDI_SYSEX_DEVICE 0x0B /* Code for this device  */
#define MIDI_SYSEX_PROTOCOL_VERSION 0x00 /* Use this as an internal protocol version tracker */

/* MIDI commands. These represent the upper 4 bits of the
 * command byte where the lower 4 bits (excepting sysex)
 * represent the MIDI channel.
 */
enum {
    kMidiMessageNoteOff = 0x80,
    kMidiMessageNoteOn = 0x90,
    kMidiMessageAftertouchPoly = 0xA0,
    kMidiMessageControlChange = 0xB0,
    kMidiMessageProgramChange = 0xC0,
    kMidiMessageAftertouchChannel = 0xD0,
    kMidiMessagePitchWheel = 0xE0,
    kMidiMessageSysex = 0xF0,
    kMidiMessageSysexEnd = 0xF7,
    kMidiMessageActiveSense = 0xFE,
    kMidiMessageReset = 0xFF
};

/* Special MIDI CC numbers that might be used */
enum {
    kMidiControlDataEntryMSB = 6,
    kMidiControlDataEntryLSB = 38,
    kMidiControlDamper = 64,
    kMidiControlSostenuto = 66,
    kMidiControlUnaCorda = 67,
    kMidiControlNonRegisteredParameterLSB = 98,
    kMidiControlNonRegisteredParameterMSB = 99,
    kMidiControlRegisteredParameterLSB = 100,
    kMidiControlRegisteredParameterMSB = 101,
    kMidiControlAllSoundOff = 120,
    kMidiControlAllControllersOff = 121,
    kMidiControlLocalControl = 122,
    kMidiControlAllNotesOff = 123
};


enum {
    kMidiSysexCommandAcknowledge = 0x01, /* ACK or NAK */
    kMidiSysexCommandIdentify = 0x02, /* Request to (or response to) identify */
    kMidiSysexCommandGet = 0x03, // Request current values of parameter
    kMidiSysexCommandBooted = 0x04, // The device has booted
    kMidiSysexCommandRawKeySensorData = 0x10, /* Data from analog sensors: raw */
    kMidiSysexCommandCalibratedPositionData = 0x11, /* Data from analog sensors: calibrated position */
    kMidiSysexCommandCalibratedPressureData = 0x12, /* Data from analog sensors: calibrated pressure */
    kMidiSysexCommandErrorMessage = 0x1F, /* Error message from the device */
    kMidiSysexCommandTransmissionOptions = 0x30, /* Set what gets transmitted from scanner */
    kMidiSysexCommandStartScanning = 0x40, /* start data gathering */
    kMidiSysexCommandStopScanning = 0x41, /* stop data gathering */
    kMidiSysexCommandStartCalibration = 0x44, /* start calibration */
    kMidiSysexCommandFinishCalibration = 0x45, /* finish and confirm calibration */
    kMidiSysexCommandAbortCalibration = 0x46, /* abort and discard calibration */
    kMidiSysexCommandResetCalibration = 0x47, /* reset to uncalibrated state */
    kMidiSysexCommandSetNoteThresholds = 0x4C, /* set thresholds for MIDI note on/off */
    kMidiSysexCommandSetVelocityCurve = 0x4D, /* set velocity curve for MIDI note on/off */
    kMidiSysexCommandSetReverse = 0x4E, /* set whether the key's readings should be reversed (sensing surface is further
                                           away when at rest, closest when pressed) */
    kMidiSysexCommandSetLEDColor = 0x60, /* set color of one or more RGB LEDs */
    kMidiSysexCommandAllLEDsOff = 0x61, /* turn off all LEDs */
    kMidiSysexCommandSetAutoLEDBehaviour = 0x62, /* set the internal LED behaviour */
    kMidiSysexCommandJumpToBootloader = 0x77 /* jump to bootloader */
};
