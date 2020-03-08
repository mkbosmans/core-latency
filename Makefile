CXXFLAGS=-std=c++11 -Wall -Wno-parentheses -pthread -lnuma -O3

SOURCES=main.cpp

core-latency: $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: distclean test

distclean:
	rm core-latency

test: core-latency
	@./core-latency
