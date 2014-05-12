CXX=g++
CXXFLAGS=-Wall -g -lboost_system -lboost_program_options --std=c++11

all: klient serwer

klient: klient.h klient.cpp
	$(CXX) $(CXXFLAGS) klient.cpp -o klient

serwer: serwer.h mixer.cpp serwer.cpp
	$(CXX) $(CXXFLAGS) mixer.cpp serwer.cpp -o serwer

clean:
	rm -f klient serwer
