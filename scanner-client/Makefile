OBJS := rtmidi/RtMidi.o main.o lib/KeyScannerClient.o
CPPFLAGS := -Irtmidi -Ilib
CXXFLAGS := -std=c++11 -O0 -g -D__MACOSX_CORE__
LDLIBS := -framework CoreMIDI -framework CoreAudio -framework CoreServices

BIN := scanner-client

all: $(BIN)

$(BIN):	$(OBJS)
	$(CXX) $^ -o $@ $(LDLIBS)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(OBJS) $(BIN)
