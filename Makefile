CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
CPPFLAGS ?= -D_FILE_OFFSET_BITS=64

.PHONY: all clean test test-large
all: tracker/tracker client/client

tracker/tracker: $(wildcard tracker/src/*.cpp tracker/include/*.h common/src/*.cpp common/include/*.h)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tracker/src/*.cpp common/src/*.cpp -o $@

client/client: $(wildcard client/src/*.cpp client/include/*.h common/src/*.cpp common/include/*.h)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) client/src/*.cpp common/src/*.cpp -o $@

clean:
	rm -f tracker/tracker client/client tests/unit

# Python is only the black-box test driver; application code is C++.
tests/unit: tests/unit.cpp tracker/src/state.cpp $(wildcard tracker/include/*.h common/include/*.h common/src/*.cpp)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/unit.cpp tracker/src/state.cpp common/src/*.cpp -o $@

test: all tests/unit
	./tests/unit
	python3 tests/integration.py
	python3 tests/failures.py
	python3 tests/trackerRecovery.py

test-large: all
	python3 tests/integration.py --large
