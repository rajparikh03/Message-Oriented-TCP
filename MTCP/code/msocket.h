#ifndef MSOCKET_H
#define MSOCKET_H

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/shm.h>
#include <sys/sem.h>
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

#define MAX_SOCKETS 25
#define MAX_WINDOW_SIZE 5
#define MAX_SEND_BUFFER_SIZE 10
#define MAX_RECV_BUFFER_SIZE 5
#define MESSAGE_SIZE 1024
#define MAX_SEQ_NO 15
#define DATA_PACKET_SIZE 2 + MESSAGE_SIZE
#define ACK_PACKET_SIZE 3
#define SOCK_MTP 1 // Custom socket type for MTP sockets
#define T 5        // Timeout value in seconds
#define P 0.5

/*

seq_num -> a b c = 1 2 3
rwnd    -> a b c = 0 1 2

*/

// Structure to represent a message

typedef struct
{
    // An integer representing the sequence number of the message
    int seq_num;
    // A character array of size MESSAGE_SIZE (1024 bytes) to store the message data.
    char data[MESSAGE_SIZE];

} Message;

typedef struct
{
    // An integer flag indicating whether the packet is an acknowledgment (ACK) packet or a data packet.
    char is_ack;
    // An integer representing the sequence number of the packet.
    char seq_num;

    union
    {
        // A character array of size MESSAGE_SIZE (1024 bytes) to store the message data if the packet is a data packet.
        char data[MESSAGE_SIZE];
        // An integer representing the receive window size if the packet is an ACK packet.
        char rwnd;
    };
} Data_Packet;

// Structure to represent a window
typedef struct
{
    // An integer representing the size of the window.
    int size;
    // for swnd it defines the base at the buffer
    // for recv it defines the idx for the seq_num for easy implementation
    int base;
    // An array of integers to store sequence numbers up to MAX_SEQ_NO (15).
    int seq_nums[MAX_SEQ_NO + 1];
    // for send, the seq no of the last message which was sent
    // for recv, next expected to receive seq_num from the buffer
    int last_seq_num;
    // for send, the seq no the last acknowleged message
    int last_ack_seq;
} Window;

// Structure to represent an MTP socket entry in the shared memory
typedef struct
{
    // An integer flag indicating whether the socket entry is free or in use.
    int is_free;
    // The process ID of the process that owns the socket.
    pid_t process_id;
    // The underlying UDP socket ID.
    int udp_socket_id;
    // The remote address (struct sockaddr_in) for the socket.
    struct sockaddr_in remote_addr;
    // An array of Message structures to store messages for sending.
    Message send_buffer[MAX_SEND_BUFFER_SIZE];
    // An array of Message structures to store received messages.
    Message recv_buffer[MAX_RECV_BUFFER_SIZE];
    // A Window structure representing the send window
    Window swnd;
    // A Window structure representing the receive window.
    Window rwnd;
    // A timespec structure to store the last time a message was sent on this socket.
    struct timespec last_send_time;
    // // An integer used for locking/synchronization.
    // int lock;
} MTPSocketEntry;

typedef struct
{
    // An integer representing the socket ID.
    int sockid;
    // An integer representing the domain of the socket (e.g., AF_INET).
    int domain;
    // A sockaddr_in structure representing the address of the socket.
    struct sockaddr_in addr;
    //  An integer to store an error code.
    int err_no;
    // An integer used for locking/synchronization.
    // int lock;
    // The process ID of the process that owns the socket.
    pid_t process_id;
} SockInfoEntry;

// Function prototypes
int m_socket(int domain, int type, int protocol);
int m_bind(int sockfd, const struct sockaddr_in src_addr, const struct sockaddr_in dest_addr);
int m_sendto(int sockfd, const char *buf, size_t len, int flags, const struct sockaddr_in dest_addr, socklen_t addrlen);
int m_recvfrom(int sockfd, char *buf, int len, int flags, struct sockaddr_in src_addr, socklen_t *addrlen);
int m_close(int sockfd);
int dropMessage(float p);

void *r_thread(void *arg);
void receive_message(MTPSocketEntry *sock, int i);
void handle_data_msg(MTPSocketEntry *sock, int seq_num, Message *data, int i);
void handle_ack_msg(MTPSocketEntry *sock, int ack_num, int rwnd_size, int ack, int i);
void store_message(MTPSocketEntry *sock, Message *data, int i);
void send_ack(MTPSocketEntry *sock, int seq_num, int rwnd_size, int flag, int i);
void update_swnd(MTPSocketEntry *sock, int ack_num, int rwnd_size, int i);
void *s_thread(void *arg);
void check_timeouts(MTPSocketEntry *entry, int x);
void send_pending_messages(MTPSocketEntry *entry, int x);
void sigchld_handler(int sig);

// Error codes
#define ENOTBOUND 1 // Socket is not bound

#define sem_Wait(s) semop(s, &pop, 1)   /* pop is the structure we pass for doing the P(s) operation */
#define sem_Signal(s) semop(s, &vop, 1) /* vop is the structure we pass for doing the V(s) operation */

#define sem_Wait1(s) semop(s, &pop1, 1)   /* pop is the structure we pass for doing the P(s) operation */
#define sem_Signal1(s) semop(s, &vop1, 1) /* vop is the structure we pass for doing the V(s) operation */

#define max(a, b) (a > b) ? a : b

#endif
