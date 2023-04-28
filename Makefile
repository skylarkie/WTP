CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -pedantic -std=c++11

.PHONY: clean all

all: wSender wReceiver

clean:
	rm -f wSender wReceiver $(OBJS)

wSender: wSender.cpp ../starter_files/PacketHeader.h ../starter_files/crc32.h
	$(CXX) $(CFLAGS) -o wSender wSender.cpp

wReceiver: wReceiver.cpp ../starter_files/PacketHeader.h ../starter_files/crc32.h
	$(CXX) $(CFLAGS) -o wReceiver wReceiver.cpp