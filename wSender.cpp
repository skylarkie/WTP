/*
      UDP based WTP sender
      could read an input file and transmit it to a specified receiver using UDP following the WTP protocol
*/
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
      // ./wSender <receiver-IP> <receiver-port> <window-size> <input-file> <log>
      if (argc != 6)
      {
            printf("Usage: ./wSender <receiver-IP> <receiver-port> <window-size> <input-file> <log>\n");
            exit(1);
      }
      // initialize variables
      char *receiver_ip = argv[1];
      unsigned short receiver_port = atoi(argv[2]);
      unsigned short window_size = atoi(argv[3]);
      char *input_file = argv[4];
      char *log_file = argv[5];

      // open log file & input file
      FILE *log = fopen(log_file, "w");
      if (log == NULL)
      {
            printf("Fail to open log file.\n");
            exit(1);
      }
      FILE *input = fopen(input_file, "r");
      if (input == NULL)
      {
            printf("Fail to open input file.\n");
            exit(1);
      }

      // create socket
      int s;
      if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
      {
            perror("Fail to create socket.\n");
            exit(1);
      }
      struct sockaddr_in addr_sender;
      memset((char *)&addr_sender, 0, sizeof(addr_sender));
      addr_sender.sin_family = AF_INET;
      addr_sender.sin_addr.s_addr = inet_addr(receiver_ip); // ip address
      addr_sender.sin_port = htons(receiver_port);          // port

      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 500000;
      if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
      {
            perror("Error setting socket options");
            return 0;
      }

      // send the file
      unsigned int seq_num = 0;
      unsigned int rand_num = rand() % 2048;
      unsigned int ack_num = 0;
      unsigned int max_seq_num = 65535;

      // send START packet
      struct packet start_packet;
      memset(&start_packet, 0, sizeof(start_packet));
      start_packet.header.seqNum = rand_num;
      start_packet.header.type = 0;
      start_packet.header.length = 0;
      start_packet.header.checksum = 0;
      if (sendto(s, &start_packet, sizeof(start_packet), 0, (struct sockaddr *)&addr_sender, sizeof(addr_sender)) == -1)
      {
            perror("Fail to send START packet.\n");
            exit(1);
      }
      printf("START packet sent.\n");
      fprintf(log, "%d %d %d %d\n", start_packet.header.type, start_packet.header.seqNum, start_packet.header.length, start_packet.header.checksum);

      // wait for START ACK
      struct packet start_ack_packet;
      memset(&start_ack_packet, 0, sizeof(start_ack_packet));
      // set start time
      std::chrono::milliseconds start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      double time_elapsed = 0;
      while (time_elapsed <= 500)
      {
            if (recvfrom(s, &start_ack_packet, sizeof(start_ack_packet), 0, NULL, NULL) == -1)
            {
                  perror("Fail to receive START ACK packet.\n");
                  exit(1);
            }
            // set current time
            std::chrono::milliseconds current_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
            time_elapsed = static_cast<double>(current_time.count() - start_time.count());

            if (start_ack_packet.header.type == 2 && start_ack_packet.header.seqNum == rand_num)
            {
                  printf("START ACK received.\n");
                  fprintf(log, "%d %d %d %d\n", start_ack_packet.header.type, start_ack_packet.header.seqNum, start_ack_packet.header.length, start_ack_packet.header.checksum);
                  break;
            }
            else
            {
                  printf("START ACK not received.\n");
                  fprintf(log, "%d %d %d %d\n", start_ack_packet.header.type, start_ack_packet.header.seqNum, start_ack_packet.header.length, start_ack_packet.header.checksum);
            }
      }
      if (time_elapsed > 500)
      {
            printf("START Timeout.\n");
            exit(1);
      }

      // send DATA packets
      int DONE = 0;
      unsigned int window = 0;
      while (!DONE)
      {
            fseek(input, seq_num * MAX_BUF_SIZE, SEEK_SET);
            struct packet data_packet;
            memset(&data_packet, 0, sizeof(data_packet));
            data_packet.header.seqNum = seq_num;
            data_packet.header.type = 1;
            data_packet.header.length = fread(data_packet.data, 1, MAX_BUF_SIZE, input);
            data_packet.header.checksum = crc32((unsigned char *)&data_packet, sizeof(data_packet));
            // implement sliding window and only cumulative ACK is sent
            if (data_packet.header.length == 0)
            {
                  max_seq_num = seq_num;
            }
            if (window < window_size && seq_num <= max_seq_num)
            {
                  if (sendto(s, &data_packet, sizeof(data_packet), 0, (struct sockaddr *)&addr_sender, sizeof(addr_sender)) == -1)
                  {
                        perror("Fail to send DATA packet.\n");
                        exit(1);
                  }
                  printf("DATA packet sent.\n");
                  fprintf(log, "%d %d %d %d\n", data_packet.header.type, data_packet.header.seqNum, data_packet.header.length, data_packet.header.checksum);
                  window++;
                  seq_num++;
            }
            else
            {
                  struct packet ack_packet;
                  memset(&ack_packet, 0, sizeof(ack_packet));
                  // set start time
                  std::chrono::milliseconds start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
                  double time_elapsed = 0;
                  while (time_elapsed <= 500)
                  {
                        if (recvfrom(s, &ack_packet, sizeof(ack_packet), 0, NULL, NULL) == -1)
                        {
                              perror("Fail to receive DATA ACK packet.\n");
                              exit(1);
                        }
                        // set current time
                        std::chrono::milliseconds current_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
                        time_elapsed = static_cast<double>(current_time.count() - start_time.count());
                        if (ack_packet.header.type == 2 && ack_packet.header.seqNum >= ack_num)
                        {
                              printf("DATA ACK received.\n");
                              seq_num = ack_packet.header.seqNum + 1;
                              ack_num = ack_packet.header.seqNum + 1;
                              window = 0;
                              fprintf(log, "%d %d %d %d\n", ack_packet.header.type, ack_packet.header.seqNum, ack_packet.header.length, ack_packet.header.checksum);
                              break;
                        }
                        if (ack_packet.header.type == 2 && ack_packet.header.seqNum == max_seq_num)
                        {
                              printf("final DATA ACK received.\n");
                              DONE = 1;
                        }
                  }
                  if (time_elapsed > 500)
                  {
                        seq_num = ack_num;
                        window = 0;
                  }
            }
      }

      // send END packet
      struct packet end_packet;
      memset(&end_packet, 0, sizeof(end_packet));
      end_packet.header.seqNum = rand_num;
      end_packet.header.type = 1;
      end_packet.header.length = 0;
      end_packet.header.checksum = 0;
      if (sendto(s, &end_packet, sizeof(end_packet), 0, (struct sockaddr *)&addr_sender, sizeof(addr_sender)) == -1)
      {
            perror("Fail to send END packet.\n");
            exit(1);
      }
      printf("END packet sent.\n");
      fprintf(log, "%d %d %d %d\n", end_packet.header.type, end_packet.header.seqNum, end_packet.header.length, end_packet.header.checksum);

      // wait for END ACK
      struct packet end_ack_packet;
      memset(&end_ack_packet, 0, sizeof(end_ack_packet));
      std::chrono::milliseconds start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      double time_elapsed = 0;
      while (time_elapsed <= 500)
      {
            if (recvfrom(s, &end_ack_packet, sizeof(end_ack_packet), 0, NULL, NULL) == -1)
            {
                  perror("Fail to receive START ACK packet.\n");
                  exit(1);
            }
            // set current time
            std::chrono::milliseconds current_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
            time_elapsed = static_cast<double>(current_time.count() - start_time.count());

            if (end_ack_packet.header.type == 2 && end_ack_packet.header.seqNum == rand_num)
            {
                  printf("END ACK received.\n");
                  fprintf(log, "%d %d %d %d\n", end_ack_packet.header.type, end_ack_packet.header.seqNum, end_ack_packet.header.length, end_ack_packet.header.checksum);
                  break;
            }
            else
            {
                  printf("END ACK not received.\n");
                  fprintf(log, "%d %d %d %d\n", end_ack_packet.header.type, end_ack_packet.header.seqNum, end_ack_packet.header.length, end_ack_packet.header.checksum);
            }
      }
      if (time_elapsed > 500)
      {
            printf("END Timeout.\n");
            exit(1);
      }
      // close
      fclose(input);
      fclose(log);
      close(s);
      return 0;
}