CXX := clang++
SDKROOT := $(shell xcrun --show-sdk-path)
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -g -isysroot $(SDKROOT)
CPPFLAGS := -I. -Ithird_party/compat -isystem $(SDKROOT)/usr/include/c++/v1
BUILD_DIR := build
SRC_DIR := src
TESTS_DIR := tests

POLONIO_BIN := $(BUILD_DIR)/polonio
POLONIO_TEST_BIN := $(BUILD_DIR)/polonio_tests

SRC_FILES := $(SRC_DIR)/main.cpp
TEST_FILES := $(TESTS_DIR)/test_main.cpp

all: $(POLONIO_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(POLONIO_BIN): $(BUILD_DIR) $(SRC_FILES)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(SRC_FILES) -o $@

$(POLONIO_TEST_BIN): $(BUILD_DIR) $(TEST_FILES) third_party/doctest/doctest.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TEST_FILES) -o $@

test: $(POLONIO_TEST_BIN)
	$(POLONIO_TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test clean
