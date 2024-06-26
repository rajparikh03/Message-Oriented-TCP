// user2.c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include "msocket.h"

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        printf("Usage: %s <local_ip> <local_port> <remote_ip> <remote_port>\n", argv[0]);
        return 1;
    }

    // Create an MTP socket
    int sockfd = m_socket(AF_INET, SOCK_MTP, 0);
    if (sockfd == -1)
    {
        perror("m_socket");
        return 1;
    }

    // Bind the MTP socket
    struct sockaddr_in local_addr, remote_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(atoi(argv[2]));
    local_addr.sin_addr.s_addr = inet_addr(argv[1]);
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(atoi(argv[4]));
    remote_addr.sin_addr.s_addr = inet_addr(argv[3]);

    if (m_bind(sockfd, local_addr, remote_addr) == -1)
    {
        perror("m_bind");
        return 1;
    }

    char buffer[MESSAGE_SIZE];
    int bytes;
    char filename[100];
    sprintf(filename, "output%d.txt", atoi(argv[2]));
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1)
    {
        perror("Error opening file");
        return 1;
    }

    int cnt = 0;
    while (1)
    {
        socklen_t addrlen = sizeof(remote_addr);
        int recv_len = MESSAGE_SIZE;
        bytes = m_recvfrom(sockfd, buffer, recv_len, 0, remote_addr, &addrlen);
        if (bytes > 0)
        {
            int bytes_written = write(fd, buffer, bytes);
            if (bytes_written == -1)
            {
                perror("Error writing to file");
                close(fd);
                return 1;
            }
            cnt++;
            printf("Data Packet %d received successfully\n", cnt);
            if (bytes < recv_len)
            {
                break;
            }
        }
        else if (bytes == 0)
        {
            printf("Connection closed by peer.\n");
            break;
        }
    }

    printf("Total number of messages received = %d\n", cnt);

    // Close the output file
    close(fd);
    // m_close(sockfd);

    return 0;
}