#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <vector>
#include "../starter_files/PacketHeader.h"
#include "../starter_files/crc32.h"

#define MAX_BUF_SIZE 1456

struct packet
{
      struct PacketHeader header;
      char data[MAX_BUF_SIZE];
};

void log_packet(char *log, struct packet *p)
{
      //<type> <seqNum> <length> <checksum>
      std::ofstream logFile;
      logFile.open(log, std::ios::app);
      logFile << p->header.type << " " << p->header.seqNum << " " << p->header.length << " " << p->header.checksum << std::endl;
      printf("%d %d %d %d\n", p->header.type, p->header.seqNum, p->header.length, p->header.checksum);
      logFile.close();
}

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
      socklen_t len = sizeof(cliaddr);
      servaddr.sin_family = AF_INET;
      servaddr.sin_addr.s_addr = INADDR_ANY;
      servaddr.sin_port = htons(port);

      // bind socket to port
      if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
      {
            perror("bind failed");
            exit(EXIT_FAILURE);
      }

      // initialize variables
      unsigned int ack_num = 0;
      unsigned int start_num = 0;
      unsigned int file_num = 0;
      FILE *file = NULL;

      std::vector<packet> window;

      // receive packets
      while (true)
      {
            struct packet p;
            int n = recvfrom(sockfd, (char *)&p, sizeof(p), MSG_WAITALL, (struct sockaddr *)&cliaddr, &len);
            if (n > 0)
            {
                  // check if packet is corrupted
                  if (p.header.checksum != crc32((unsigned char *)&p.data, p.header.length) && p.header.type == 2)
                  {
                        printf("Packet corrupted\n");
                        continue;
                  }
                  // check the packet length and size
                  if (static_cast<size_t>(n) < sizeof(packet))
                  {
                        printf("Recv Length: %d. Failed to receive the full packet.\n", n);
                        continue;
                  }
                  // handle START packet
                  if (p.header.type == 0)
                  {
                        printf("START packet received\n");
                        // log <type> <seqNum> <length> <checksum>
                        log_packet(log, &p);
                        // send ACK packet
                        struct packet ack;
                        ack.header.type = 3;
                        ack.header.seqNum = p.header.seqNum;
                        ack.header.length = 0;
                        ack.header.checksum = 0;
                        sendto(sockfd, (const char *)&ack, sizeof(ack), MSG_CONFIRM, (const struct sockaddr *)&cliaddr, len);
                        printf("START ACK packet sent\n");
                        log_packet(log, &ack);
                        // create file: <output-dir>/FILE-<file-num>.out
                        char filename[100];
                        sprintf(filename, "%s/FILE-%d.out", outputDir, file_num);
                        file = fopen(filename, "w");
                        if (file == NULL)
                        {
                              perror("Error");
                              exit(EXIT_FAILURE);
                        }
                        file_num++;
                  }
                  // handle END packet
                  else if (p.header.type == 1)
                  {
                        printf("END packet received\n");
                        // log <type> <seqNum> <length> <checksum>
                        log_packet(log, &p);
                        // send ACK packet
                        struct packet ack;
                        ack.header.type = 3;
                        ack.header.seqNum = p.header.seqNum;
                        ack.header.length = 0;
                        ack.header.checksum = 0;
                        sendto(sockfd, (const char *)&ack, sizeof(ack), MSG_CONFIRM, (const struct sockaddr *)&cliaddr, len);
                        printf("ACK packet sent\n");
                        log_packet(log, &ack);
                        break;
                  }
                  // handle DATA packet
                  // implemnet GBN
                  else if (p.header.type == 2)
                  {
                        printf("DATA packet received\n");
                        // log <type> <seqNum> <length> <checksum>
                        log_packet(log, &p);
                        // check if the packet is in the window
                        if (p.header.seqNum >= start_num && p.header.seqNum < start_num + window_size)
                        {
                              // check if the packet is a duplicate
                              if (p.header.seqNum < start_num + window.size())
                              {
                                    printf("Duplicate packet received\n");
                                    // log <type> <seqNum> <length> <checksum>
                                    log_packet(log, &p);
                                    // send ACK packet
                                    struct packet ack;
                                    ack.header.type = 3;
                                    ack.header.seqNum = p.header.seqNum;
                                    ack.header.length = 0;
                                    ack.header.checksum = 0;
                                    sendto(sockfd, (const char *)&ack, sizeof(ack), MSG_CONFIRM, (const struct sockaddr *)&cliaddr, len);
                                    printf("ACK packet sent\n");
                                    log_packet(log, &ack);
                                    continue;
                              }
                              // add the packet to the window
                              printf("here\n");
                              window.push_back(p);
                              // send ACK packet
                              struct packet ack;
                              ack.header.type = 3;
                              ack.header.seqNum = p.header.seqNum;
                              ack.header.length = 0;
                              ack.header.checksum = 0;
                              sendto(sockfd, (const char *)&ack, sizeof(ack), MSG_CONFIRM, (const struct sockaddr *)&cliaddr, len);
                              printf("ACK packet sent\n");
                              // check if the packet is the first packet in the window
                              if (p.header.seqNum == start_num)
                              {
                                    // write the data to the file
                                    fwrite(p.data, sizeof(char), p.header.length, file);
                                    // update the start number
                                    start_num++;
                                    // check if there are more packets in the window
                                    while (!window.empty())
                                    {
                                          // check if the next packet is in the window
                                          if (window[0].header.seqNum == start_num)
                                          {
                                                // write the data to the file
                                                fwrite(window[0].data, sizeof(char), window[0].header.length, file);
                                                // update the start number
                                                start_num++;
                                                // remove the packet from the window
                                                window.erase(window.begin());
                                          }
                                          else
                                          {
                                                break;
                                          }
                                    }
                              }
                        }
                  }
            }
      }
      // close the socket
      close(sockfd);
      return 0;
}