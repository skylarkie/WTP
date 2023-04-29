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

      // open input file
      FILE *fp = fopen(input_file, "rb");
      if (fp == NULL)
      {
            perror("Fail to open input file.\n");
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
      // unsigned int ack_num = 0;
      // unsigned int max_seq_num = 65535;

      // send START packet
      struct packet start_packet;
      memset(&start_packet, 0, sizeof(start_packet));
      start_packet.header.seqNum = rand_num;
      start_packet.header.type = 0;
      start_packet.header.length = 0;
      start_packet.header.checksum = 0;
      unsigned int slen = sizeof(addr_sender);
      if (sendto(s, &start_packet, sizeof(start_packet), 0, (struct sockaddr *)&addr_sender, slen) == -1)
      {
            perror("Fail to send START packet.\n");
            exit(1);
      }
      printf("START packet sent.\n");
      log_packet(log_file, &start_packet);

      // wait for START ACK
      struct packet start_ack_packet;
      memset(&start_ack_packet, 0, sizeof(start_ack_packet));

      int attempts = 0;
      while (attempts <= 10)
      {
            if (recvfrom(s, &start_ack_packet, sizeof(start_ack_packet), 0, (struct sockaddr *)&addr_sender, &slen) == -1)
            {
                  perror("Fail to receive START ACK packet.\n");
                  exit(1);
            }
            if (start_ack_packet.header.type == 3 && start_ack_packet.header.seqNum == rand_num)
            {
                  printf("START ACK received.\n");
                  log_packet(log_file, &start_ack_packet);
                  break;
            }
            else
            {
                  printf("START ACK not received.\n");
            }
            attempts++;
      }
      if (attempts > 10)
      {
            printf("Fail to receive START ACK packet.\n");
            exit(1);
      }

      // send DATA packets
      // int DONE = 0;
      // unsigned int window = 0;
      std::vector<struct packet> packets;
      // count the number of packets
      while (!feof(fp))
      {
            struct packet p;
            memset(&p, 0, sizeof(p));
            p.header.seqNum = seq_num;
            p.header.type = 2;
            p.header.length = fread(p.data, 1, MAX_BUF_SIZE, fp);
            p.header.checksum = crc32((unsigned char *)&p.data, p.header.length);
            packets.push_back(p);
            seq_num++;
      }
      // implement GBN
      int base = 0;
      int next_seq_num = 0;
      while (base < packets.size())
      {
            // send packets
            while (next_seq_num < base + window_size && next_seq_num < packets.size())
            {
                  if (sendto(s, &packets[next_seq_num], sizeof(packets[next_seq_num]), 0, (struct sockaddr *)&addr_sender, slen) == -1)
                  {
                        perror("Fail to send DATA packet.\n");
                        exit(1);
                  }
                  printf("DATA packet sent.\n");
                  log_packet(log_file, &packets[next_seq_num]);
                  next_seq_num++;
            }
            // receive ACKs
            struct packet ack_packet;
            memset(&ack_packet, 0, sizeof(ack_packet));
            if (recvfrom(s, &ack_packet, sizeof(ack_packet), 0, (struct sockaddr *)&addr_sender, &slen) == -1)
            {
                  // timeout and resend
                  printf("timeout.\n");
                  next_seq_num = base;
                  continue;
            }
            printf("ACK packet received.\n");
            log_packet(log_file, &ack_packet);
            // shift window according to cumulative ACK
            if (ack_packet.header.type == 3 && ack_packet.header.seqNum >= base && ack_packet.header.seqNum < base + window_size)
            {
                  base = ack_packet.header.seqNum + 1;
            }
            else
            {
                  printf("ACK packet not received.\n");
                  next_seq_num = base;
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
      log_packet(log_file, &end_packet);
      // wait for END ACK
      struct packet end_ack_packet;
      memset(&end_ack_packet, 0, sizeof(end_ack_packet));
      std::chrono::milliseconds start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
      double time_elapsed = 0;
      while (time_elapsed <= 500)
      {
            if (recvfrom(s, &end_ack_packet, sizeof(end_ack_packet), 0, NULL, NULL) == -1)
            {
                  perror("Fail to receive END ACK packet.\n");
                  exit(1);
            }
            // set current time
            std::chrono::milliseconds current_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
            time_elapsed = static_cast<double>(current_time.count() - start_time.count());

            if (end_ack_packet.header.type == 3 && end_ack_packet.header.seqNum == rand_num)
            {
                  printf("END ACK received.\n");
                  log_packet(log_file, &end_ack_packet);
                  break;
            }
            else
            {
                  printf("END ACK not received.\n");
            }
      }
      if (time_elapsed > 500)
      {
            printf("END Timeout.\n");
            exit(1);
      }
      // close
      close(s);
      return 0;
}