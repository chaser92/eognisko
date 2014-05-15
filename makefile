CXX=g++
CXXFLAGS=-Wall -g --std=c++11 

all: klient serwer

klient: klient.h klient.cpp
	$(CXX) $(CXXFLAGS) klient.cpp -o klient -lboost_system -lboost_program_options -lpthread

serwer: serwer.h mixer.cpp serwer.cpp
	$(CXX) $(CXXFLAGS) mixer.cpp serwer.cpp -o serwer -lboost_system -lboost_program_options -lpthread

clean:
	rm -f klient serwer
