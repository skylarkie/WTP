/*
    Simple udp server
*/
#include <arpa/inet.h>
#include <climits>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../starter_files/PacketHeader.h"
#include "../starter_files/crc32.h"

#define DEBUG 1
#define BUFLEN 512
#define DATALEN 1456

const char *output_dir_name;
const char *log_file_name;

struct Packet
{
      unsigned int type;     // 0: START; 1: END; 2: DATA; 3: ACK
      unsigned int seqNum;   // Describe afterwards
      unsigned int length;   // Length of data; 0 for ACK packets
      unsigned int checksum; // 32-bit CRC
      char data[DATALEN];    // data transmitted
};

void clear_file(const char *file_path)
{
      // clear file content
      std::ofstream fd(file_path, std::ios::trunc);
      fd.close();
}

void log(Packet &packet, bool isRecv = false)
{
      std::ofstream fd(log_file_name, std::ios::app);
      std::string message =
          std::to_string(packet.type) + " " + std::to_string(packet.seqNum) + " " +
          std::to_string(packet.length) + " " + std::to_string(packet.checksum);
      fd << message << std::endl;
      fd.close();
      if (DEBUG)
      {
            if (isRecv)
            {
                  message = "recv " + message;
            }
            else
            {
                  message = "send " + message;
            }
            std::cout << message << std::endl;
      }
}

bool check_packet(Packet &packet)
{
      if (packet.checksum == crc32(packet.data, packet.length))
      {
            return true;
      }
      else
      {
            printf("Packet checksum: %u, calculated checksum: %u\n", packet.checksum,
                   crc32(packet.data, packet.length));
            return false;
      }
}

bool recv_packet(int server_socket, Packet &packet,
                 struct sockaddr_in &add_client, socklen_t &slen)
{
      void *buffer = malloc(sizeof(Packet));
      int recv_len;
      memset(buffer, '\0', sizeof(Packet));
      if ((recv_len = recvfrom(server_socket, buffer, sizeof(Packet), 0,
                               (struct sockaddr *)&add_client, &slen)) == -1)
      {
            perror("error with recvfrom().\n");
            exit(1);
      }

      // try to convert the received data to Packet in the right size
      if (static_cast<size_t>(recv_len) < sizeof(Packet))
      {
            printf("Recv Length: %d. Failed to receive the full packet.\n", recv_len);
            return false;
      }

      packet = *(Packet *)buffer;
      if (packet.type == 2 && packet.length == 0)
      {
            printf("Receive emtpy data packet. Ignore\n");
            return false;
      }
      log(packet, true);

      if (!check_packet(packet))
      {
            printf("Received packet with wrong checksum.\n");
            return false;
      }
      return true;
}

int recieve_messgae(unsigned short port, int window_size)
{
      // bool isConnectionEstablished = false;
      struct sockaddr_in addr_server, add_client;
      socklen_t slen = sizeof(add_client);
      int server_socket, file_count = -1;
      // Packet window_packets[window_size];
      Packet *window_packets = new Packet[window_size];

      // create a UDP socket
      if ((server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
      {
            perror("Fail to create socket.\n");
            exit(1);
      }

      // clear the memory for address structure
      memset((char *)&addr_server, 0, sizeof(addr_server));

      addr_server.sin_family = AF_INET;
      addr_server.sin_addr.s_addr = htonl(INADDR_ANY); // ip address
      addr_server.sin_port = htons(port);              // port adress

      // bind socket to port
      if (bind(server_socket, (struct sockaddr *)&addr_server,
               sizeof(addr_server)) == -1)
      {
            perror("Fail to bind.\n");
            exit(1);
      }

      clear_file(log_file_name);

      // keep listening for data
      while (true)
      {
            Packet packet;
            std::string output_file_path;
            unsigned int expected_seqnum = 0;
            std::vector<Packet> received_packets;
            if (!recv_packet(server_socket, packet, add_client, slen))
            {
                  printf("Error in recv packets.\n");
                  continue;
            }

            if (packet.type != 0)
            {
                  printf("Connection has not been established.\n");
                  continue;
            }
            else
            {
                  // establish connection
                  file_count++;
                  output_file_path = std::string(output_dir_name) + "/FILE-" +
                                     std::to_string(file_count) + ".out";
                  clear_file(output_file_path.c_str());
                  printf("Clear the output file\n");
                  while (packet.type == 0)
                  {
                        Packet response = {3, packet.seqNum, 0, crc32("", 0), ""};
                        log(response, false);
                        if (sendto(server_socket, &response, sizeof(response), 0,
                                   (struct sockaddr *)&add_client, slen) == -1)
                        {
                              perror("error with sendto().\n");
                              exit(1);
                        }
                        if (!recv_packet(server_socket, packet, add_client, slen))
                        {
                              printf("Error in recv packets.\n");
                              continue;
                        }
                  }

                  // start recv data
                  bool first_data_packet = true;
                  while (packet.type != 1)
                  {
                        // recv data of a window
                        received_packets.clear();
                        int idx = 0;
                        if (first_data_packet)
                        {
                              first_data_packet = false;
                              received_packets.push_back(packet);
                              idx++;
                        }
                        for (; idx < window_size; idx++)
                        {
                              if (!recv_packet(server_socket, packet, add_client, slen))
                              {
                                    // printf("Error in recv packets.\n");
                                    continue;
                              }
                              if (packet.type == 1)
                              {
                                    printf("End of Connection\n");
                                    // send the end ACK
                                    Packet response = {3, packet.seqNum, 0, crc32("", 0), ""};
                                    log(response, false);
                                    if (sendto(server_socket, &response, sizeof(response), 0,
                                               (struct sockaddr *)&add_client, slen) == -1)
                                    {
                                          perror("error with sendto().\n");
                                          exit(1);
                                    }
                                    break;
                              }
                              if (packet.type == 0)
                              {
                                    printf("Connection has already been built.\n");
                                    continue;
                              }
                              received_packets.push_back(packet);
                        }

                        // if (received_packets.empty()) {
                        //   printf("No packets received anymore, break.\n");
                        //   break;
                        // }

                        // sort the received packets according to the seqNum
                        // std::sort(received_packets.begin(), received_packets.end(),
                        //           [](const Packet &a, const Packet &b) {
                        //             return a.seqNum < b.seqNum;
                        //           });

                        // iterate the received packets to find the expected seqNum
                        size_t expected_idx = 0;
                        for (size_t i = 0; i < received_packets.size(); ++i)
                        {
                              if (received_packets[i].seqNum == expected_seqnum)
                              {
                                    expected_idx = i;
                                    break;
                              }
                        }

                        // from the expected idx to the first non-consecutive seqNum
                        size_t highest_idx = expected_idx, window_idx = 0;
                        while (highest_idx < received_packets.size() &&
                               static_cast<int>(window_idx) < window_size)
                        {
                              if (received_packets[highest_idx].type != 1 &&
                                  received_packets[highest_idx].seqNum ==
                                      expected_seqnum + window_idx)
                              {
                                    window_packets[window_idx] = received_packets[highest_idx];
                                    highest_idx++;
                                    window_idx++;
                              }
                              else
                              {
                                    break;
                              }
                        }
                        // save the data within window_idx to the file
                        for (size_t i = 0; i < window_idx; ++i)
                        {
                              // printf("output_file_path: %s\n", output_file_path.c_str());
                              std::ofstream output_file(output_file_path, std::ios::app);
                              output_file.write(window_packets[i].data, window_packets[i].length);
                              output_file.close();
                        }

                        if (packet.type == 1)
                        {
                              break;
                        }

                        // update the expected seqNum and send ACK
                        expected_seqnum += window_idx;
                        Packet response = {3, expected_seqnum, 0, crc32("", 0), ""};
                        log(response, false);
                        if (sendto(server_socket, &response, sizeof(response), 0,
                                   (struct sockaddr *)&add_client, slen) == -1)
                        {
                              perror("error with sendto().\n");
                              exit(1);
                        }
                  }
            }
      }

      delete[] window_packets;
      close(server_socket); // close the socket.
      return 0;
}

int main(int argc, const char **argv)
{
      if (argc != 5)
      {
            printf("Usage: ./wSender <port-num> <window-size> <output-dir> <log>\n");
            return 1;
      }
      int port = atoi(argv[1]);
      int window_size = atoi(argv[2]);
      output_dir_name = argv[3];
      log_file_name = argv[4];

      recieve_messgae(port, window_size);
      return 0;
}