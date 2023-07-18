# Variables
CC=g++
CXXFLAGS=-g -std=c++17

# Lists of files
COMMON_FILES=$(wildcard src/common/*.cpp)
CLIENT_FILES=$(wildcard src/client/*.cpp)
SERVER_FILES=$(wildcard src/server/*.cpp)

COMMON_OBJECTS=$(COMMON_FILES:.cpp=.o)
CLIENT_OBJECTS=$(CLIENT_FILES:.cpp=.o)
SERVER_OBJECTS=$(SERVER_FILES:.cpp=.o)

# Include directories
INCLUDES=-I src/common -I src/client -I src/server

# Linker flags
LDFLAGS=-L.

# Libraries to link against
LDLIBS=-lcommon

# Target: common library
libcommon.a: $(COMMON_OBJECTS)
	ar rvs $@ $^

# Target: client
client: $(CLIENT_OBJECTS) libcommon.a
	$(CC) $(LDFLAGS) -o $@ $(CLIENT_OBJECTS) $(LDLIBS)

# Target: server
server: $(SERVER_OBJECTS) libcommon.a
	$(CC) $(LDFLAGS) -o $@ $(SERVER_OBJECTS) $(LDLIBS)

# Pattern rule for object files
%.o: %.cpp
	$(CC) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# .PHONY rule for clean
.PHONY: clean
clean:
	rm -f src/common/*.o src/client/*.o src/server/*.o libcommon.a client server
