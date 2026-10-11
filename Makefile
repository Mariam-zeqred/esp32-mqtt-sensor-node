# Host-side checks (no board needed). `make check` is what CI runs.
CXX ?= g++
CXXFLAGS ?= -std=gnu++17 -Wall -Wextra -Werror -O1
CORE_SRC = $(wildcard lib/node_core/*.cpp)

.PHONY: check test-core test-py lint smoke pio-test build certs clean

check: lint test-core test-py

test-core: build/test_core
	./build/test_core

build/test_core: test/test_native/test_core.cpp test/minitest.h $(CORE_SRC) lib/node_core/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Ilib/node_core -Itest test/test_native/test_core.cpp $(CORE_SRC) -o $@

test-py:
	python3 -m pytest -q

lint:
	ruff check tools tests
	ruff format --check tools tests

# Same core tests, run by PlatformIO (needs the registry reachable to fetch the native platform).
pio-test:
	pio test -e native

# TLS broker -> subscriber -> CSV, all on this machine. Needs certs/ (make certs) and amqtt.
smoke:
	python3 tools/smoke_test.py

certs:
	tools/make_certs.sh 127.0.0.1 localhost

# Firmware (needs the espressif32 platform; run on your machine, not checked here).
build:
	pio run -e esp32dev

clean:
	rm -rf build .pio
