/*
    Simple udp client
*/
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <ostream>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#include <fstream>
#include <string>
#include <chrono>
#include <algorithm>

#include "../starter_files/crc32.h"

// #define SERVER "localhost"
#define DATALEN 1456

struct Packet
{
    unsigned int type;     // 0: START; 1: END; 2: DATA; 3: ACK
    unsigned int seqNum;   // Describe afterwards
    unsigned int length;   // Length of data; 0 for ACK packets
    unsigned int checksum; // 32-bit CRC
    char data[DATALEN];    // data transmitted
};

int send_file(const char *server, unsigned short port, int window_size, const char *input_file, const char *log)
{

    /* ----- establish connection ----- */

    struct sockaddr_in addr_server;
    int s;
    socklen_t slen = sizeof(addr_server);
    Packet ACK;

    // create socket
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        perror("fail to create socket.\n");
        exit(1);
    }

    // clear the memory for address structure
    memset((char *)&addr_server, 0, sizeof(addr_server));
    addr_server.sin_family = AF_INET;
    struct hostent *sp = gethostbyname(server);              // get hostname
    memcpy(&addr_server.sin_addr, sp->h_addr, sp->h_length); // ip address
    addr_server.sin_port = htons(port);                      // port

    std::ifstream infile(input_file, std::ifstream::binary);
    std::ofstream outfile(log, std::ofstream::binary);
    unsigned int rand_seq_num = rand() % 100; // generate a random number between 0 and 99

    // start transmission
    char tmp[] = "";
    Packet packet = {0, rand_seq_num, 0, crc32(tmp, 0), *tmp};

    while (true)
    {
        if (sendto(s, &packet, sizeof(packet), 0, (struct sockaddr *)&addr_server, slen) == -1)
        {
            perror("error with sendto().\n");
            exit(1);
        }

        std::string str_tmp = std::to_string(packet.type) + " " + std::to_string(packet.seqNum) + " " + std::to_string(packet.length) + " " + std::to_string(packet.checksum) + "\n";
        const char *str = str_tmp.c_str();
        outfile.write(str, strlen(str));

        // start timestamp
        std::chrono::milliseconds time_start =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch());

        double duration_lf = 0.0f;

        while (duration_lf <= 500)
        {
            // try to receive ACK
            int ret = recvfrom(s, &ACK, sizeof(ACK), MSG_DONTWAIT, (struct sockaddr *)&addr_server, &slen);

            // current timestamp
            std::chrono::milliseconds time_end =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch());

            // time window
            int64_t duration = time_end.count() - time_start.count();
            duration_lf = static_cast<double>(duration);

            // check whether ACK is received
            if (ret > 0 && ACK.type == 3 && ACK.seqNum == rand_seq_num)
            {
                std::string str_tmp = std::to_string(ACK.type) + " " + std::to_string(ACK.seqNum) + " " + std::to_string(ACK.length) + " " + std::to_string(ACK.checksum) + "\n";
                const char *str = str_tmp.c_str();
                outfile.write(str, strlen(str));
                break;
            }
        }

        if (duration_lf <= 500)
        {
            break;
        }
    }

    /* ----- transmit data ----- */
    printf("start transmitting data...\n");

    // get length of file:
    infile.seekg(0, infile.end);
    int length = infile.tellg();
    infile.seekg(0, infile.beg);

    unsigned int seq_num = 0;
    unsigned int next = 0;

    Packet buffer[window_size];

    while (next * DATALEN < length)
    {

        int resend_seq = seq_num - next;

        for (int i = 0; i < resend_seq; i++)
        {
            int index = i + window_size - resend_seq;
            buffer[i] = buffer[index];
            printf("Send packet (%u, %u, %u, %u))\n", buffer[index].type, buffer[index].seqNum, buffer[index].length, buffer[index].checksum);
            if (sendto(s, &buffer[index], sizeof(buffer[index]), 0, (struct sockaddr *)&addr_server, slen) == -1)
            {
                perror("error with sendto().\n");
                exit(1);
            }
            if (buffer[index].length > 0)
            {
                std::string str_tmp = std::to_string(buffer[index].type) + " " + std::to_string(buffer[index].seqNum) + " " + std::to_string(buffer[index].length) + " " + std::to_string(buffer[index].checksum) + "\n";
                const char *str = str_tmp.c_str();
                outfile.write(str, strlen(str));
            }
        }

        int n = window_size - (seq_num - next);

        int j = 0;
        for (int i = 0; i < n; i++)
        {
            char data[DATALEN];
            infile.read(data, DATALEN);
            unsigned int len = infile.gcount();

            buffer[i + resend_seq] = {2, seq_num, len, crc32(data, len), *data};
            memcpy(buffer[i + resend_seq].data, data, len);

            printf("Send packet (%u, %u, %u, %u))\n", buffer[i + resend_seq].type, buffer[i + resend_seq].seqNum, buffer[i + resend_seq].length, buffer[i + resend_seq].checksum);
            if (sendto(s, &buffer[i + resend_seq], sizeof(buffer[i + resend_seq]), 0, (struct sockaddr *)&addr_server, slen) == -1)
            {
                perror("error with sendto().\n");
                exit(1);
            }

            std::string str_tmp = std::to_string(buffer[i + resend_seq].type) + " " + std::to_string(buffer[i + resend_seq].seqNum) + " " + std::to_string(buffer[i + resend_seq].length) + " " + std::to_string(buffer[i + resend_seq].checksum) + "\n";
            const char *str = str_tmp.c_str();
            outfile.write(str, strlen(str));

            seq_num += 1;
            j += 1;

            if (infile.eof())
            {
                break;
            }
        }

        while (j < n)
        {
            char tmp[] = "";
            Packet packet = {2, seq_num, 0, crc32(tmp, 0), *tmp};
            buffer[window_size - n + j] = packet;
            printf("Send packet (%u, %u, %u, %u))\n", packet.type, packet.seqNum, packet.length, packet.checksum);
            if (sendto(s, &packet, sizeof(packet), 0, (struct sockaddr *)&addr_server, slen) == -1)
            {
                perror("error with sendto().\n");
                exit(1);
            }
            seq_num += 1;
            j += 1;
        }

        // start timestamp
        std::chrono::milliseconds time_start =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch());

        double duration_lf = 0.0f;

        while (duration_lf <= 500)
        {
            // try to receive ACK
            int ret = recvfrom(s, &ACK, sizeof(ACK), MSG_DONTWAIT, (struct sockaddr *)&addr_server, &slen);

            // current timestamp
            std::chrono::milliseconds time_end =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch());

            // time window
            int64_t duration = time_end.count() - time_start.count();
            duration_lf = static_cast<double>(duration);

            // check whether ACK is received
            if (ret > 0 && ACK.type == 3 && ACK.seqNum >= next && ACK.seqNum <= next + window_size)
            {
                std::string str_tmp = std::to_string(ACK.type) + " " + std::to_string(ACK.seqNum) + " " + std::to_string(ACK.length) + " " + std::to_string(ACK.checksum) + "\n";
                const char *str = str_tmp.c_str();
                printf("Receive ACK (%u, %u, %u, %u)\n", ACK.type, ACK.seqNum, ACK.length, ACK.checksum);
                outfile.write(str, strlen(str));
                next = ACK.seqNum;
                break;
            }
        }
    }

    /* ----- terminate connection ----- */

    Packet terminate = {1, rand_seq_num, 0, crc32(tmp, 0), *tmp};

    while (true)
    {
        if (sendto(s, &terminate, sizeof(terminate), 0, (struct sockaddr *)&addr_server, slen) == -1)
        {
            perror("error with sendto().\n");
            exit(1);
        }

        std::string str_tmp = std::to_string(terminate.type) + " " + std::to_string(terminate.seqNum) + " " + std::to_string(terminate.length) + " " + std::to_string(terminate.checksum) + "\n";
        const char *str = str_tmp.c_str();
        outfile.write(str, strlen(str));

        // start timestamp
        std::chrono::milliseconds time_start =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch());

        double duration_lf = 0.0f;

        while (duration_lf <= 500)
        {
            // try to receive ACK
            int ret = recvfrom(s, &ACK, sizeof(ACK), MSG_DONTWAIT, (struct sockaddr *)&addr_server, &slen);

            // current timestamp
            std::chrono::milliseconds time_end =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch());

            // time window
            int64_t duration = time_end.count() - time_start.count();
            duration_lf = static_cast<double>(duration);

            // check whether ACK is received
            if (ret > 0 && ACK.type == 3 && ACK.seqNum == rand_seq_num)
            {
                std::string str_tmp = std::to_string(ACK.type) + " " + std::to_string(ACK.seqNum) + " " + std::to_string(ACK.length) + " " + std::to_string(ACK.checksum) + "\n";
                const char *str = str_tmp.c_str();
                outfile.write(str, strlen(str));
                break;
            }
        }

        if (duration_lf <= 500)
        {
            break;
        }
    }

    outfile.close();
    infile.close();
    close(s); // close socket
    return 0;
}

int main(int argc, const char **argv)
{
    // Parse command line arguments
    if (argc != 6)
    {
        printf("Usage: ./wSender <receiver-IP> <receiver-port> <window-size> <input-file> <log>\n");
        return 1;
    }
    const char *receiver_ip = argv[1];
    unsigned short receiver_port = strtoul(argv[2], NULL, 10);
    int window_size = atoi(argv[3]);
    const char *input_file = argv[4];
    const char *log = argv[5];

    int send_client = send_file(receiver_ip, receiver_port, window_size, input_file, log);

    return 0;
}