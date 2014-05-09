CXX=g++
CXXFLAGS=-Wall -lboost_system --std=c++11

all: klient serwer

klient: klient.h klient.cpp
	$(CXX) $(CXXFLAGS) klient.cpp -o klient

serwer: serwer.h serwer.cpp
	$(CXX) $(CXXFLAGS) serwer.cpp -o serwer

clean:
	rm -f klient serwer