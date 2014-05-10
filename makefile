CXX=g++
CXXFLAGS=-Wall -lboost_system --std=c++11

all: klient serwer

klient: klient.h klient.cpp
	$(CXX) $(CXXFLAGS) klient.cpp -o klient

serwer: serwer.h mixer.cpp serwer.cpp
	$(CXX) $(CXXFLAGS) mixer.cpp serwer.cpp -o serwer

clean:
	rm -f klient serwer