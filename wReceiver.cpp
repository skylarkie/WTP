#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fstream>
#include <chrono>
#include <algorithm>
#include "PacketHeader.h"
#include "crc32.h"

#define MAX_BUF_SIZE 1456

struct packet
{
      struct PacketHeader header;
      char data[MAX_BUF_SIZE];
};

int main(int argc, char *argv[])
{
      //./wReceiver <port-num> <window-size> <output-dir> <log>
      if (argc != 5)
      {
            printf("Usage: %s <port-num> <window-size> <output-dir> <log>\n", argv[0]);
            exit(1);
      }
      // initialize variables
      int port = atoi(argv[1]);
      int window_size = atoi(argv[2]);
      char *outputDir = argv[3];
      char *log = argv[4];
      int sockfd;
      struct sockaddr_in servaddr, cliaddr;

      // create UDP socket
      if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
      {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
      }
      memset(&servaddr, 0, sizeof(servaddr));
      memset(&cliaddr, 0, sizeof(cliaddr));
      servaddr.sin_family = AF_INET;
      servaddr.sin_addr.s_addr = INADDR_ANY;
      servaddr.sin_port = htons(port);

      // bind socket to port
      if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
      {
            perror("bind failed");
            exit(EXIT_FAILURE);
      }

      // set timeout
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 500000;
      if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
      {
            perror("Error");
      }

      // initialize variables
}