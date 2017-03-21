
CXXFLAGS=--std=c++11
LINKARGS=-lev

all: clean client server

server:
	c++ $(CXXFLAGS) $(LINKARGS) -o server server.cpp

client:
	c++ $(CXXFLAGS) $(LINKARGS) -o client client.cpp

clean:
	@rm -f client server
