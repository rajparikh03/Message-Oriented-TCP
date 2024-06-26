#define _POSIX_C_SOURCE 199309L

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
#include "msocket.h"
#include <semaphore.h>
#include <errno.h>
#include <arpa/inet.h>

struct sembuf pop, vop;
MTPSocketEntry *sm;
int semid1, semid2;
SockInfoEntry *sockinfo;
int locks[MAX_SOCKETS];
int cnt;
int adjid, sockinfoid;
int sockinfolock;

int nospace_flag[MAX_SOCKETS] = {0};

void *r_thread(void *arg)
{
    printf("\n\n******************R thread started******************\n\n");
    fd_set read_fds;
    struct timeval timeout;
    int max_fd, nfds;
    timeout.tv_sec = T;
    timeout.tv_usec = 0;

    while (1)
    {
        // Clear the read file descriptor set
        FD_ZERO(&read_fds);

        // Find the maximum file descriptor value
        max_fd = -1;

        // Add all active UDP sockets to the read set
        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            sem_Wait(locks[i]);

            if (sm[i].is_free == 0)
            {
                FD_SET(sm[i].udp_socket_id, &read_fds);
                max_fd = max(max_fd, sm[i].udp_socket_id);
            }
            sem_Signal(locks[i]);
        }
        // Wait for data to be available on any of the UDP sockets
        nfds = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (nfds == -1)
        {
            // Error in select
            perror("select");
            continue;
        }
        else if (nfds == 0)
        {

            printf("\033[0;34m");
            printf("\n**********Timeout R thread*********\n");
            printf("\033[0m");
            timeout.tv_sec = T; // Set timeout to T seconds
            timeout.tv_usec = 0;
            // Timeout occurred, check if any sockets need duplicate ACKs
            for (int i = 0; i < MAX_SOCKETS; i++)
            {
                sem_Wait(locks[i]);

                if (sm[i].is_free == 0 && (nospace_flag[i] && sm[i].rwnd.size > 0))
                {
                    if (sm[i].rwnd.base == 1)
                        send_ack(&sm[i], MAX_SEQ_NO, sm[i].rwnd.size, 2, i);
                    else
                        send_ack(&sm[i], sm[i].rwnd.base - 1, sm[i].rwnd.size, 2, i);
                }
                sem_Signal(locks[i]);
            }
        }
        else
        {

            printf("\033[0;34m");
            printf("\n******Activity R thread*******\n");
            printf("\033[0m");
            // Data available on at least one UDP socket
            for (int i = 0; i < MAX_SOCKETS; i++)
            {
                sem_Wait(locks[i]);

                if (sm[i].is_free == 0 && FD_ISSET(sm[i].udp_socket_id, &read_fds))
                {
                    receive_message(&sm[i], i);
                }
                sem_Signal(locks[i]);
            }
        }
    }

    return NULL;
}

void receive_message(MTPSocketEntry *sock, int i)
{
    Data_Packet *packet = (Data_Packet *)malloc(sizeof(Data_Packet));

    // Receive message from UDP socket
    socklen_t cli_len = sizeof(sock->remote_addr);
    recvfrom(sock->udp_socket_id, (Data_Packet *)packet, DATA_PACKET_SIZE, 0, (struct sockaddr *)&(sock->remote_addr), &cli_len);

    int drop = dropMessage(P);

    if (drop)
    {
        if (packet->is_ack - 'a' != 0)
        {
            printf("\033[0;32m");
            printf("SOCKET %d : Ack with seq_num = %d, rwnd = %d, is_ack = %d dropped\n", i, packet->seq_num - 'a', packet->rwnd - 'a', packet->is_ack - 'a');
            printf("\033[0m");
        }
        else
        {
            printf("\033[0;32m");
            printf("SOCKET %d : Data message with seq_num = %d dropped\n", i, packet->seq_num - 'a');
            printf("\033[0m");
        }
        return;
    }

    if (packet->is_ack - 'a' != 0)
    {
        // Handle ACK message
        handle_ack_msg(sock, packet->seq_num - 'a', packet->rwnd - 'a', packet->is_ack - 'a', i);
    }
    else
    {
        // Handle data message
        Message message;
        message.seq_num = packet->seq_num - 'a';
        strncpy(message.data, packet->data, MESSAGE_SIZE);
        handle_data_msg(sock, packet->seq_num - 'a', &message, i);
    }
}

void handle_data_msg(MTPSocketEntry *sock, int seq_num, Message *message, int i)
{
    // checking whether data is inside the window
    if ((seq_num < sock->rwnd.base && seq_num + MAX_SEQ_NO - sock->rwnd.base < sock->rwnd.size) || (sock->rwnd.base <= seq_num && seq_num < sock->rwnd.base + sock->rwnd.size))
    {
        // checking for duplicate messages
        if (sock->rwnd.seq_nums[seq_num] == 1)
        {
            sock->rwnd.seq_nums[seq_num] = 0;
            printf("\033[0;31m");
            printf("SOCKET %d : Receiving data with seq_num = %d \n", i, seq_num);
            printf("\033[0m");

            store_message(sock, message, i);

            // sending suitable ACK
            if (sock->rwnd.seq_nums[sock->rwnd.base] == 1)
            {
                if (sock->rwnd.base == 1)
                    send_ack(sock, MAX_SEQ_NO, sock->rwnd.size, 1, i);
                else
                    send_ack(sock, sock->rwnd.base - 1, sock->rwnd.size, 1, i);
            }
            else
            {
                // adjusting window
                while (sock->rwnd.seq_nums[sock->rwnd.base] == 0 && sock->rwnd.size > 0)
                {
                    if (sock->rwnd.base == MAX_SEQ_NO)
                    {
                        sock->rwnd.base = 1;
                    }
                    else
                        sock->rwnd.base++;

                    sock->rwnd.size--;
                    if (nospace_flag[i] == 1)
                        nospace_flag[i] = 0;
                }

                // if rwnd is full making the nospace flag 1
                if (sock->rwnd.size == 0)
                    nospace_flag[i] = 1;

                // sending ACK to the last inorder message
                if (sock->rwnd.base == 1)
                    send_ack(sock, MAX_SEQ_NO, sock->rwnd.size, 1, i);
                else
                    send_ack(sock, sock->rwnd.base - 1, sock->rwnd.size, 1, i);
            }
        }
        else
        {
            // sending ACK to the last inorder message
            if (sock->rwnd.base == 1)
                send_ack(sock, MAX_SEQ_NO, sock->rwnd.size, 1, i);
            else
                send_ack(sock, sock->rwnd.base - 1, sock->rwnd.size, 1, i);
        }
    }
    else
    {
        // sending ACK to the last inorder message
        if (sock->rwnd.base == 1)
            send_ack(sock, MAX_SEQ_NO, sock->rwnd.size, 1, i);
        else
            send_ack(sock, sock->rwnd.base - 1, sock->rwnd.size, 1, i);
    }
}

void handle_ack_msg(MTPSocketEntry *sock, int ack_num, int rwnd_size, int ack, int x)
{
    // Update sender window based on ACK
    if (ack == 1)
    {
        update_swnd(sock, ack_num, rwnd_size, x);
    }
    else if (ack == 2)
    {
        update_swnd(sock, ack_num, rwnd_size, x);
        int flag = 0;
        for (int i = 0; i < MAX_SEND_BUFFER_SIZE; i++)
        {
            if (sock->send_buffer[i].seq_num != -2)
            {
                flag = 1;
                break;
            }
        }
        if (flag == 0)
        {
            send_ack(sock, ack_num, rwnd_size, 3, x);
        }
    }
    else if (ack == 3)
    {
        nospace_flag[x] = 0;
    }
}

void store_message(MTPSocketEntry *sock, Message *message, int x)
{
    // Store the message in the receive buffer
    int i = message->seq_num % MAX_RECV_BUFFER_SIZE; ////////// 'i' WILL CHANGE IF RECV WINDOW SIZE OR RECV BUFFER SIZE CHANGE.

    for (int j = 0; j < MESSAGE_SIZE; j++)
    {
        sock->recv_buffer[i].data[j] = message->data[j];
    }
    printf("\033[0;31m");
    printf("SOCKET %d : Message stored at idx = %d \n", x, i);
    printf("\033[0m");
    sock->recv_buffer[i].seq_num = message->seq_num;
}

void send_ack(MTPSocketEntry *sock, int seq_num, int rwnd_size, int flag, int i)
{
    Data_Packet ack_packet;
    ack_packet.is_ack = flag + 'a';
    ack_packet.seq_num = seq_num + 'a';
    ack_packet.rwnd = rwnd_size + 'a';
    printf("\033[0;31m");
    printf("SOCKET %d : ACK sent with seq_num %d , rwnd %d, is_ack %d \n", i, seq_num, rwnd_size, flag);
    printf("\033[0m");

    sendto(sock->udp_socket_id, (Data_Packet *)&ack_packet, ACK_PACKET_SIZE, 0,
           (struct sockaddr *)&sock->remote_addr, sizeof(sock->remote_addr));
}

void update_swnd(MTPSocketEntry *sock, int ack_num, int rwnd_size, int x)
{
    if (sock->swnd.last_ack_seq != ack_num)
    {
        // Update the sender window based on the received ACK
        while (sock->send_buffer[sock->swnd.base].seq_num != ack_num)
        {
            sock->swnd.seq_nums[sock->send_buffer[sock->swnd.base].seq_num] = 0;
            sock->send_buffer[sock->swnd.base].seq_num = -2;
            sock->swnd.base++;
            sock->swnd.base %= MAX_SEND_BUFFER_SIZE;
        }
        sock->swnd.seq_nums[sock->send_buffer[sock->swnd.base].seq_num] = 0;
        sock->send_buffer[sock->swnd.base].seq_num = -2;
        sock->swnd.base++;
        sock->swnd.base %= MAX_SEND_BUFFER_SIZE;
    }
    sock->swnd.size = rwnd_size;
    sock->swnd.last_ack_seq = ack_num;

    // Checking for new messages to send.
    for (int i = 0; i < sock->swnd.size; i++)
    {
        int buf_idx = sock->swnd.base + i;
        buf_idx %= MAX_SEND_BUFFER_SIZE;
        if (sock->send_buffer[buf_idx].seq_num == -1)
        {
            sock->swnd.last_seq_num++;
            if (sock->swnd.last_seq_num == MAX_SEQ_NO + 1)
                sock->swnd.last_seq_num = 1;
            sock->send_buffer[buf_idx].seq_num = sock->swnd.last_seq_num;
        }
    }

    printf("\033[0;31m");
    printf("SOCKET %d : update base %d , size %d \n", x, sock->swnd.base, sock->swnd.size);
    printf("\033[0m");
}

void *s_thread(void *arg)
{
    printf("\n\n******************S thread started******************\n\n");
    struct timespec timeout;
    timeout.tv_sec = T / 2.0;
    timeout.tv_nsec = 0;
    while (1)
    {
        nanosleep(&timeout, NULL);
        printf("\n\t\t*********S thread wakes up*********\n");

        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            if (sm[i].is_free == 0)
            {
                // Lock the shared memory entry for this socket
                sem_Wait(locks[i]);

                // Check for message timeouts
                check_timeouts(&sm[i], i);

                // Send pending messages
                send_pending_messages(&sm[i], i);

                sem_Signal(locks[i]);
            }
        }
    }

    return NULL;
}

void check_timeouts(MTPSocketEntry *entry, int x)
{
    struct timespec current_time;
    clock_gettime(CLOCK_REALTIME, &current_time);

    // Calculate the time elapsed since the last transmission
    long long elapsed_ns = (current_time.tv_sec - entry->last_send_time.tv_sec) * 1000000000LL +
                           (current_time.tv_nsec - entry->last_send_time.tv_nsec);

    if (elapsed_ns >= T * 1000000000LL)
    {
        // Timeout occurred, retransmit all messages in the sender window
        for (int i = 0; i < entry->swnd.size; i++)
        {
            uint16_t buffer_idx = (entry->swnd.base + i) % MAX_SEND_BUFFER_SIZE;
            Message message = entry->send_buffer[buffer_idx];

            if (message.seq_num > 0)
            {
                entry->swnd.seq_nums[message.seq_num] = 1;
                Data_Packet *datapacket = (Data_Packet *)malloc(sizeof(Data_Packet));
                datapacket->is_ack = 0 + 'a';
                datapacket->seq_num = message.seq_num + 'a';
                strncpy(datapacket->data, message.data, MESSAGE_SIZE);
                printf("\t\tSOCKET %d : retransmit-message with seq_num = %d \n", x, message.seq_num);

                cnt++;
                sendto(entry->udp_socket_id, (Data_Packet *)datapacket, DATA_PACKET_SIZE, 0,
                       (struct sockaddr *)&entry->remote_addr, sizeof(entry->remote_addr));
            }
        }
        // Update the last send time
        clock_gettime(CLOCK_REALTIME, &entry->last_send_time);
    }
}

void send_pending_messages(MTPSocketEntry *entry, int x)
{
    for (int i = 0; i < entry->swnd.size; i++)
    {
        uint16_t buffer_idx = (entry->swnd.base + i) % MAX_SEND_BUFFER_SIZE;
        Message message = entry->send_buffer[buffer_idx];

        if (message.seq_num > 0)
        {
            if (entry->swnd.seq_nums[message.seq_num] == 0)
            {
                // sending pending messages
                entry->swnd.seq_nums[message.seq_num] = 1;
                Data_Packet *datapacket = (Data_Packet *)malloc(sizeof(Data_Packet));
                datapacket->is_ack = 0 + 'a';
                datapacket->seq_num = message.seq_num + 'a';
                strncpy(datapacket->data, message.data, MESSAGE_SIZE);
                cnt++;
                printf("\t\tSOCKET %d : sent message with seq_num = %d\n", x, message.seq_num);
                sendto(entry->udp_socket_id, (Data_Packet *)datapacket, DATA_PACKET_SIZE, 0,
                       (struct sockaddr *)&entry->remote_addr, sizeof(entry->remote_addr));
            }
        }
    }
}

void *gc_thread(void *arg)
{
    printf("\n\n******************Garbage Collector thread started******************\n\n");
    struct timespec timeout;
    timeout.tv_sec = 100 * T;
    timeout.tv_nsec = 0;

    while (1)
    {
        nanosleep(&timeout, NULL);

        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            sem_Wait(locks[i]);
            if (sm[i].is_free == 2)
            {

                memset(&sm[i], 0, sizeof(sm[i]));
                sm[i].is_free = 1;

                printf("Socket entry %d cleared\n", i);
            }
            else if (sm[i].is_free == 0)
            {
                int status = kill(sm[i].process_id, 0);
                printf("Process id -> %d\n", sm[i].process_id);
                printf("STATUS, sock entry-> %d %d\n", status, i);
                if (status != 0)
                {

                    memset(&sm[i], 0, sizeof(sm[i]));
                    sm[i].is_free = 1;

                    printf("Socket entry %d cleared\n", i);
                }
            }
            sem_Signal(locks[i]);
        }
    }
}

void sigint_handler(int signum)
{
    printf("\nCtrl+C pressed. Cleaning up...\n");
    printf("Total number of transmissions = %d \n", cnt);
    printf("\n");
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        if (semctl(locks[i], 0, IPC_RMID) == -1)
        {
            perror("semctl");
            exit(EXIT_FAILURE);
        }
    }
    semctl(sockinfolock, 0, IPC_RMID);

    shmdt(sm);
    shmdt(sockinfo);

    // Detach and remove shared memory segment
    if (shmctl(adjid, IPC_RMID, 0) == -1)
    {
        perror("shmctl1");
        exit(EXIT_FAILURE);
    }
    if (shmctl(sockinfoid, IPC_RMID, NULL) == -1)
    {
        perror("shmctl2");
        exit(EXIT_FAILURE);
    }

    if (semctl(semid1, 0, IPC_RMID) == -1)
    {
        perror("semctl3");
        exit(EXIT_FAILURE);
    }

    if (semctl(semid2, 0, IPC_RMID) == -1)
    {
        perror("semctl2");
        exit(EXIT_FAILURE);
    }

    printf("Shared memory segment destroyed. Exiting.\n");
    exit(EXIT_SUCCESS);
}

int main()
{
    printf("\n\n******************Init process started ******************\n\n");

    if (signal(SIGINT, sigint_handler) == SIG_ERR)
    {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    if (signal(SIGTSTP, sigint_handler) == SIG_ERR)
    {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    key_t valkey = ftok(".", 95);
    adjid = shmget(valkey, MAX_SOCKETS * sizeof(MTPSocketEntry), 0777 | IPC_CREAT);
    // printf("adj -> %d\n", adjid);
    sm = (MTPSocketEntry *)shmat(adjid, 0, 0);

    valkey = ftok(".", 97);
    sockinfoid = shmget(valkey, sizeof(SockInfoEntry), 0777 | IPC_CREAT);
    // printf("sockinfo -> %d\n", sockinfoid);
    sockinfo = (SockInfoEntry *)shmat(sockinfoid, 0, 0);

    valkey = ftok(".", 98);
    semid1 = semget(valkey, 1, 0777 | IPC_CREAT);
    valkey = ftok(".", 99);
    semid2 = semget(valkey, 1, 0777 | IPC_CREAT);

    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = 0;
    pop.sem_op = -1;
    vop.sem_op = 1;

    // Initialize the shared memory
    memset(sm, 0, MAX_SOCKETS * sizeof(MTPSocketEntry));
    memset(sockinfo, 0, sizeof(SockInfoEntry));

    valkey = ftok(".", 19);
    sockinfolock = semget(valkey, 1, 0777 | IPC_CREAT);
    semctl(sockinfolock, 0, SETVAL, 1);

    semctl(semid1, 0, SETVAL, 0);
    semctl(semid2, 0, SETVAL, 0);

    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        sm[i].is_free = 1;

        valkey = ftok(".", 20 + i);
        locks[i] = semget(valkey, 1, 0777 | IPC_CREAT);
        semctl(locks[i], 0, SETVAL, 1);
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_t r_thread_id, s_thread_id, gc_thread_id;
    pthread_create(&r_thread_id, &attr, r_thread, NULL);
    pthread_create(&s_thread_id, &attr, s_thread, NULL);
    pthread_create(&gc_thread_id, &attr, gc_thread, NULL);

    pthread_join(r_thread_id, NULL);
    pthread_join(s_thread_id, NULL);
    pthread_join(gc_thread_id, NULL);

    while (1)
    {
        sem_Wait(semid1);

        ///////////  SOCKET CREATION
        if (sockinfo->sockid == 0 && sockinfo->addr.sin_addr.s_addr == htonl(0) && sockinfo->addr.sin_port == htonl(0))
        {
            printf("\033[0;32m");
            printf("\nCreating NEW SOCKET\n");
            printf("\033[0m");
            int i;
            for (i = 0; i < MAX_SOCKETS; i++)
            {
                if (sm[i].is_free == 1)
                {
                    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
                    if (udp_socket < 0)
                    {
                        sockinfo->sockid = -1;
                        sockinfo->err_no = errno;
                        printf("\nERROR: Socket creation failed\n");
                        break;
                    }
                    sm[i].is_free = 0;
                    sm[i].process_id = sockinfo->process_id;
                    sm[i].udp_socket_id = udp_socket;

                    for (int j = 0; j < MAX_SEND_BUFFER_SIZE; j++)
                    {
                        sm[i].send_buffer[j].seq_num = -2;
                    }
                    sm[i].swnd.base = 0;
                    sm[i].swnd.size = MAX_WINDOW_SIZE;
                    sm[i].swnd.last_seq_num = 0;
                    sm[i].swnd.last_ack_seq = 15;
                    for (int j = 0; j < MAX_SEQ_NO + 1; j++)
                    {
                        sm[i].swnd.seq_nums[j] = 0;
                    }

                    for (int j = 0; j < MAX_RECV_BUFFER_SIZE; j++)
                    {
                        sm[i].recv_buffer[j].seq_num = -2;
                    }
                    sm[i].rwnd.base = 1;
                    sm[i].rwnd.size = MAX_WINDOW_SIZE;
                    sm[i].rwnd.last_seq_num = 0;

                    for (int j = 0; j < MAX_SEQ_NO + 1; j++)
                    {
                        sm[i].rwnd.seq_nums[j] = 1;
                    }

                    sm[i].last_send_time.tv_sec = -1;
                    sm[i].last_send_time.tv_nsec = -1;
                    sockinfo->sockid = udp_socket;
                    printf("\033[0;32m");
                    printf("\nSUCCESS: New socket created successfully \n");
                    printf("\033[0m");
                    break;
                }
            }
        }
        ///////////  SOCKET BINDING
        else if (sockinfo->sockid != 0)
        {
            printf("\033[0;32m");
            printf("\nBinding SOCKET %d\n", sockinfo->sockid);
            printf("\033[0m");
            int idx = -1;
            for (int i = 0; i < MAX_SOCKETS; i++)
            {
                if (sm[i].udp_socket_id == sockinfo->sockid)
                {
                    idx = i;
                    break;
                }
            }

            if (sockinfo->sockid < 0 || idx == -1)
            {
                printf("\nERROR: Socket %d exist.\n", sockinfo->sockid);
                errno = ENOTBOUND;
                sockinfo->sockid = -1;
                sockinfo->err_no = errno;
                sem_Signal(semid2);
                continue;
            }

            if (sockinfo->sockid < 0 || sm[idx].is_free == 1)
            {
                printf("\nERROR: Socket %d closed.\n", sockinfo->sockid);
                errno = EINVAL;
                sockinfo->sockid = -1;
                sockinfo->err_no = errno;
                sem_Signal(semid2);
                continue;
            }

            int bind_result = bind(sm[idx].udp_socket_id, (struct sockaddr *)&sockinfo->addr, sizeof(sockinfo->addr));
            if (bind_result < 0)
            {
                printf("\nERROR: Socket %d binding unsuccesful.\n", sockinfo->sockid);
                sockinfo->sockid = -1;
                sockinfo->err_no = errno;
                sem_Signal(semid2);

                continue;
            }
            printf("\033[0;32m");
            printf("\nSUCESS: Socket binding successful.\n");
            printf("\033[0m");
        }

        sem_Signal(semid2);
    }

    shmctl(adjid, IPC_RMID, NULL);
    shmctl(sockinfoid, IPC_RMID, NULL);
    semctl(semid1, 0, IPC_RMID, 0);
    semctl(semid2, 0, IPC_RMID, 0);

    return 0;
}