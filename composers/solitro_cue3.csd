<CsoundSynthesizer>
<CsOptions>
; Select audio/midi flags here according to platform
; Audio out   Audio in
-odac -Q2
</CsOptions>
<CsInstruments>

sr = 44100
ksmps = 100
nchnls = 2


gihandle OSCinit 8000
gibasenote init 48


instr 99
	OSCsend 2, "127.0.0.1", 7770, "/mrp/midi", "iii", 145, p4, 0
endin

instr 100
	OSCsend 1, "127.0.0.1", 7770, "/mrp/midi", "iii", 145, p4, 127

endin

instr 101
	midiout 192, 0, 0, 0
	turnoff
endin

instr 102
	ain1, ain2 diskin2 "solitro_morgen2.wav", 1
	outs ain1, ain2
endin

</CsInstruments>
<CsScore>

i101 0 1 0
i102 0.25 20

e
</CsScore>
</CsoundSynthesizer>