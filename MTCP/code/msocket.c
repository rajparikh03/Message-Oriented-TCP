#include "msocket.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

struct sembuf pop1, vop1;
MTPSocketEntry *sm1;
int semid11, semid22;
SockInfoEntry *sockinfo1;
int locks1[MAX_SOCKETS];
int sockinfolock1;

bool sockaddr_in_equal(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_family == b->sin_family &&
           a->sin_port == b->sin_port &&
           a->sin_addr.s_addr == b->sin_addr.s_addr;
}

void lock_init()
{
    key_t valkey = ftok(".", 95);
    int shmid = shmget(valkey, MAX_SOCKETS * sizeof(MTPSocketEntry), 0777 | IPC_CREAT);
    sm1 = (MTPSocketEntry *)shmat(shmid, 0, 0);

    valkey = ftok(".", 97);
    int sockinfoid = shmget(valkey, sizeof(SockInfoEntry), 0777 | IPC_CREAT);
    sockinfo1 = (SockInfoEntry *)shmat(sockinfoid, 0, 0);

    valkey = ftok(".", 19);
    sockinfolock1 = semget(valkey, 1, 0777 | IPC_CREAT);

    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        valkey = ftok(".", 20 + i);
        locks1[i] = semget(valkey, 1, 0777 | IPC_CREAT);
    }

    valkey = ftok(".", 98);
    semid11 = semget(valkey, 1, 0777 | IPC_CREAT);

    valkey = ftok(".", 99);
    semid22 = semget(valkey, 1, 0777 | IPC_CREAT);

    pop1.sem_num = vop1.sem_num = 0;
    pop1.sem_flg = vop1.sem_flg = 0;
    pop1.sem_op = -1;
    vop1.sem_op = 1;
}

int m_socket(int domain, int type, int protocol)
{
    printf("\033[0;34m");
    printf("\n M_SOCKET executed...\n");
    printf("\033[0m");

    lock_init();

    if (type != SOCK_MTP)
    {
        errno = EINVAL;
        shmdt(sm1);
        shmdt(sockinfo1);
        return -1;
    }

    sem_Wait1(sockinfolock1);

    sockinfo1->sockid = 0;
    sockinfo1->addr.sin_addr.s_addr = htonl(0);
    sockinfo1->addr.sin_port = htons(0);
    sockinfo1->err_no = 0;
    sockinfo1->domain = domain;
    sockinfo1->process_id = getpid();

    sem_Signal1(semid11);

    sem_Wait1(semid22);

    int return_value = -1;
    if (sockinfo1->sockid == -1)
    {
        printf("\033[0;34m");
        printf("\nM_SOCKET: ERROR: socket creation failed \n");
        printf("\033[0m");
        errno = ENOBUFS;
        return_value = -1;
    }
    else
    {
        printf("\033[0;34m");
        printf("\nM_SOCKET: SUCCESS: socket created at sockid %d \n", sockinfo1->sockid);
        printf("\033[0m");
        return_value = sockinfo1->sockid;
    }

    sem_Signal1(sockinfolock1);
    shmdt(sm1);
    shmdt(sockinfo1);

    return return_value;
}

int m_bind(int sockfd, struct sockaddr_in src_addr, struct sockaddr_in dest_addr)
{
    printf("\033[0;34m");
    printf("\n M_BIND executed...\n");
    printf("\033[0m");

    lock_init();

    sem_Wait1(sockinfolock1);
    int idx = -1;
    int return_value = -1;
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        sem_Wait1(locks1[i]);

        if (sm1[i].udp_socket_id != sockfd)
        {
            sem_Signal1(locks1[i]);
            continue;
        }

        idx = i;

        if (sockfd < 0 || idx == -1)
        {
            errno = ENOTBOUND;
            printf("\033[0;34m");
            printf("M_BIND: ERROR: socket creation failed \n");
            printf("\033[0m");
            return_value = -1;
            break;
        }
        if (sockfd < 0 || sm1[idx].is_free)
        {
            errno = EINVAL;
            printf("\033[0;34m");
            printf("M_BIND: ERROR: socket closed\n");
            printf("\033[0m");
            return_value = -1;
            break;
        }

        sm1[idx].remote_addr = dest_addr;

        printf("M_BIND: Src_addr: %s:%d\n", inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));
        printf("M_BIND: Dest_addr: %s:%d\n", inet_ntoa(dest_addr.sin_addr), ntohs(dest_addr.sin_port));
        sockinfo1->sockid = sockfd;
        sockinfo1->addr.sin_family = src_addr.sin_family;
        sockinfo1->addr.sin_addr = src_addr.sin_addr;
        sockinfo1->addr.sin_port = src_addr.sin_port;
        sockinfo1->err_no = 0;

        sem_Signal1(semid11);
        sem_Wait1(semid22);

        if (sockinfo1->sockid == -1)
        {
            errno = EINVAL;
            printf("\033[0;34m");
            printf("M_BIND: ERROR: socket creation failed \n");
            printf("\033[0m");

            return_value = -1;
        }
        else
        {
            printf("\033[0;34m");
            printf("\t\tM_BIND: SUCCESS: socket %d binded successfully\n", sockfd);
            printf("\033[0m");

            return_value = sockinfo1->sockid;
        }

        sem_Signal1(locks1[i]);

        break;
    }
    sem_Signal1(sockinfolock1);

    shmdt(sm1);
    shmdt(sockinfo1);

    return return_value;
}

int m_sendto(int sockfd, const char *buf, size_t len, int flags,
             struct sockaddr_in dest_addr, socklen_t addrlen)
{
    lock_init();

    int idx = -1;
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        sem_Wait1(locks1[i]);
        if (sm1[i].udp_socket_id != sockfd)
        {
            sem_Signal1(locks1[i]);
            continue;
        }

        idx = i;
        if (!sockaddr_in_equal(&sm1[idx].remote_addr, &dest_addr))
        {
            errno = ENOTBOUND;
            printf("\033[0;34m");
            printf("\nM_SENDTO: ERROR: addr not same.\n");
            printf("\033[0m");
            sem_Signal1(locks1[idx]);
            shmdt(sockinfo1);
            shmdt(sm1);
            return -1;
        }

        int send_buffer_index = -1;
        for (int i = 0; i < MAX_SEND_BUFFER_SIZE; i++)
        {
            int j = sm1[idx].swnd.base + i;
            j %= MAX_SEND_BUFFER_SIZE;
            if (sm1[idx].send_buffer[j].seq_num == -2)
            {
                send_buffer_index = j;
                break;
            }
        }
        if (send_buffer_index == -1)
        {
            sem_Signal1(locks1[idx]);
            // printf("\033[0;34m");
            // printf("M_SENDTO: ERROR: No buffer exist.\n");
            // printf("\033[0m");
            errno = ENOBUFS;
            shmdt(sockinfo1);
            shmdt(sm1);
            return -1;
        }

        for (int j = 0; j < MESSAGE_SIZE; j++)
        {
            if (j < len)
                sm1[idx].send_buffer[send_buffer_index].data[j] = buf[j];
            else
                sm1[idx].send_buffer[send_buffer_index].data[j] = '\0';
        }
        sm1[idx].send_buffer[send_buffer_index].seq_num = -1;

        for (int k = 0; k < sm1[idx].swnd.size; k++)
        {
            int buf_idx = sm1[idx].swnd.base + k;
            buf_idx %= MAX_SEND_BUFFER_SIZE;
            if (sm1[idx].send_buffer[buf_idx].seq_num == -1)
            {
                sm1[idx].swnd.last_seq_num++;
                if (sm1[idx].swnd.last_seq_num == MAX_SEQ_NO + 1)
                    sm1[idx].swnd.last_seq_num = 1;
                sm1[idx].send_buffer[buf_idx].seq_num = sm1[idx].swnd.last_seq_num;
            }
        }
        sem_Signal1(locks1[i]);
    }

    if (sockfd < 0 || idx == -1)
    {
        errno = ENOTBOUND;
        printf("\033[0;34m");
        printf("\nM_SENDTO: ERROR: socket not found.\n");
        printf("\033[0m");
        shmdt(sockinfo1);
        shmdt(sm1);
        return -1;
    }

    shmdt(sockinfo1);
    shmdt(sm1);

    // len = MESSAGE_SIZE;
    return len;
}

int m_recvfrom(int sockfd, char *buf, int len, int flags,
               struct sockaddr_in src_addr, socklen_t *addrlen)
{
    lock_init();

    int cnt = 0;

    int idx = -1;
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        sem_Wait1(locks1[i]);

        if (sm1[i].udp_socket_id == sockfd && sockaddr_in_equal(&src_addr, &sm1[i].remote_addr))
        {
            idx = i;
            // Check if receive buffer has any in-order messages
            int recv_buffer_index = -1;
            int next_expected_seq_num = sm1[idx].rwnd.last_seq_num + 1;
            if (next_expected_seq_num == MAX_SEQ_NO + 1)
                next_expected_seq_num = 1;
            for (int k = 0; k < MAX_RECV_BUFFER_SIZE; k++)
            {
                int j = sm1[idx].recv_buffer[k].seq_num;
                if (j == next_expected_seq_num && sm1[idx].rwnd.seq_nums[j] == 0)
                {
                    recv_buffer_index = k;
                    break;
                }
            }
            if (recv_buffer_index == -1)
            {
                printf("\033[0;34m");
                // printf("M_RECVFROM: ERROR: no pending message to receive.\n");
                printf("\033[0m");
                errno = ENOMSG;
                sem_Signal1(locks1[idx]);
                shmdt(sockinfo1);
                shmdt(sm1);
                return -1;
            }
            for (int j = 0; j < len; j++)
            {
                if (sm1[idx].recv_buffer[recv_buffer_index].data[j] == '\0')
                    break;
                buf[j] = sm1[idx].recv_buffer[recv_buffer_index].data[j];
                cnt++;
            }
            sm1[idx].rwnd.last_seq_num++;
            if (sm1[idx].rwnd.last_seq_num == MAX_SEQ_NO + 1)
                sm1[idx].rwnd.last_seq_num = 1;
            sm1[idx].rwnd.seq_nums[sm1[idx].rwnd.last_seq_num] = 1;
            sm1[idx].recv_buffer[recv_buffer_index].seq_num = -2;
            sm1[idx].rwnd.size++;
        }

        sem_Signal1(locks1[i]);
    }

    if (sockfd < 0 || idx == -1)
    {
        printf("\033[0;34m");
        printf("\nM_RECVFROM: ERROR: socket not found.\n");
        printf("\033[0m");
        errno = ENOMSG;
        shmdt(sockinfo1);
        shmdt(sm1);
        return -1;
    }

    shmdt(sm1);
    shmdt(sockinfo1);

    return cnt;
}

int m_close(int sockfd)
{
    printf("\033[0;34m");
    printf("\t\tM_CLOSE: Closed executed for sockid %d\n", sockfd);
    printf("\033[0m");

    lock_init();

    int idx = -1;
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        sem_Wait1(locks1[i]);

        if (sm1[i].udp_socket_id == sockfd)
        {
            idx = i;
            sm1[idx].is_free = 2;
            sem_Signal1(locks1[i]);
            shmdt(sm1);
            shmdt(sockinfo1);
            return -1;
        }

        sem_Signal1(locks1[i]);
    }

    if (sockfd < 0 || idx == -1)
    {
        printf("\033[0;34m");
        printf("\nM_CLOSE: ERROR: socket not found\n");
        printf("\033[0m");
        errno = ENOTBOUND;
        shmdt(sm1);
        shmdt(sockinfo1);
        return -1;
    }

    shmdt(sm1);
    shmdt(sockinfo1);
    return 0;
}

int dropMessage(float p)
{
    if (p < 0.0f || p > 1.0f)
    {
        return 0;
    }

    float random_value = (float)rand() / (float)RAND_MAX;
    return random_value < p;
}