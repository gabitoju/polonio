CXX := clang++
SDKROOT := $(shell xcrun --show-sdk-path)
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -g -isysroot $(SDKROOT)
CPPFLAGS := -I. -Isrc -Ithird_party/compat -isystem $(SDKROOT)/usr/include/c++/v1
BUILD_DIR := build
SRC_DIR := src
TESTS_DIR := tests

POLONIO_BIN := $(BUILD_DIR)/polonio
POLONIO_TEST_BIN := $(BUILD_DIR)/polonio_tests

SRC_FILES := $(SRC_DIR)/main.cpp
COMMON_SRC := $(SRC_DIR)/polonio/common/source.cpp \
              $(SRC_DIR)/polonio/common/error.cpp \
              $(SRC_DIR)/polonio/lexer/lexer.cpp \
              $(SRC_DIR)/polonio/parser/parser.cpp \
              $(SRC_DIR)/polonio/runtime/value.cpp \
              $(SRC_DIR)/polonio/runtime/env.cpp \
              $(SRC_DIR)/polonio/runtime/output.cpp \
              $(SRC_DIR)/polonio/runtime/interpreter.cpp
TEST_FILES := $(TESTS_DIR)/test_main.cpp

all: $(POLONIO_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(POLONIO_BIN): $(BUILD_DIR) $(SRC_FILES) $(COMMON_SRC)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SRC_FILES) $(COMMON_SRC) -o $@

$(POLONIO_TEST_BIN): $(BUILD_DIR) $(TEST_FILES) $(COMMON_SRC) third_party/doctest/doctest.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TEST_FILES) $(COMMON_SRC) -o $@

test: $(POLONIO_BIN) $(POLONIO_TEST_BIN)
	$(POLONIO_TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test clean
