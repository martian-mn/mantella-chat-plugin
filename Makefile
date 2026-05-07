# Cross-compile Mantella in-game chat plugin to a Windows DLL using mingw-w64.
# No CommonLibSSE-NG dependency — uses minimal SKSE struct definitions in src/skse_minimal.h.

CXX        := x86_64-w64-mingw32-g++
TARGET     := MantellaChat.dll
BUILD_DIR  := build

CXXFLAGS := \
  -std=c++20 \
  -O2 \
  -Wall -Wextra -Wno-unused-parameter \
  -DCPPHTTPLIB_NO_EXCEPTIONS \
  -D_WIN32_WINNT=0x0601 \
  -DWIN32_LEAN_AND_MEAN \
  -DNOMINMAX \
  -Isrc \
  -Isrc/vendor \
  -static-libgcc -static-libstdc++

LDFLAGS := \
  -shared \
  -Wl,--out-implib,$(BUILD_DIR)/$(TARGET).a \
  -static-libgcc -static-libstdc++ \
  -static -lpthread \
  -lws2_32 -lcrypt32 -ladvapi32 -luser32 -lkernel32

SOURCES := src/main.cpp
OBJECTS := $(BUILD_DIR)/main.o

.PHONY: all clean
all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/main.o: src/main.cpp src/skse_minimal.h src/PrismaUI_API.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD_DIR)
