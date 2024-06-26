#include "msocket.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <arpa/inet.h>
#include "msocket.h"

int main(int argc, char *argv[])
{
    if (argc != 6)
    {
        printf("Usage: %s <local_ip> <local_port> <remote_ip> <remote_port> <input_file>\n", argv[0]);
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

    char *filename = argv[5];

    int fd = open(filename, O_RDONLY); // Open the file in read-only mode
    if (fd == -1)
    {
        perror("Error opening file");
        return 1;
    }

    char buffer[MESSAGE_SIZE];
    ssize_t bytes_read;

    for (int i = 0; i < MESSAGE_SIZE; i++)
    {
        buffer[i] = '\0';
    }
    int cnt = 0;

    // Read from the file and send data over the MTP socket
    while ((bytes_read = read(fd, buffer, MESSAGE_SIZE)) > 0)
    {
        int x = -1;
        cnt++;

        while (x == -1)
        {
            int len = bytes_read;
            x = m_sendto(sockfd, buffer, len, 0, remote_addr, sizeof(remote_addr));
        }

        printf("Data Packet %d sent successfully\n",cnt);
        for (int i = 0; i < MESSAGE_SIZE; i++)
        {
            buffer[i] = '\0';
        }
    }

    printf("Number of messages sent = %d\n", cnt);

    // Close the file
    close(fd);

    // m_close(sockfd);

    return 0;
}
