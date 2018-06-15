#Makefile
# Created on: Nov 9, 2015
#     Author: pchero


#### Compiler and tool definitions shared by all build targets #####
CC = gcc
UNAME := $(shell uname)
BASICOPTS = -Wall -g -pthread -pipe -g3 -O6 -fPIC -DAST_MODULE=\"app_tiresias\" -DAST_MODULE_SELF_SYM=__app_tiresias
CFLAGS = $(BASICOPTS)
PKGCONFIG="pkg-config"
OSLDLIBS=

# Define the build directories.
BUILD = build
TARGET = app_tiresias.so

## Target: app_voicefingerprint.so
CFLAGS += \
	-I/usr/include/ \
	-I/usr/local/include/
	
LDLIBS = $(OSLDLIBS) -laubio -lm -lcrypto

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h) 

ifeq ($(UNAME), Linux)
	SHAREDLIB_FLAGS = -shared -Xlinker -x -Wl,--hash-style=gnu -Wl,--as-needed -rdynamic
endif

ifeq ($(UNAME), Darwin)
	PKGCONFIG=$(shell if [ "x$(HOMEBREW_PREFIX)" == "x" ];then echo "/usr/local/bin/pkg-config"; else echo "$(HOMEBREW_PREFIX)/bin/pkg-config"; fi)

	# Link or archive
	SHAREDLIB_FLAGS = -bundle -Xlinker -macosx_version_min -Xlinker 10.4 -Xlinker -undefined -Xlinker dynamic_lookup -force_flat_namespace
	OSLDLIBS=/usr/lib/bundle1.o
endif

.PHONY: default all clean

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $(BUILD)/$@

#### Clean target deletes all generated files ####
default: $(BUILD) $(TARGET)

clean:
	-rm -f *.o
	-rm -rf $(BUILD)
	
install:
	mv $(BUILD)/app_tiresias.so /usr/lib/asterisk/modules/



all: default

# Create the build directory (if needed)
$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@


# Enable dependency checking
#.KEEP_STATE:
#.KEEP_STATE_FILE:.make.state.GNU-amd64-Linux