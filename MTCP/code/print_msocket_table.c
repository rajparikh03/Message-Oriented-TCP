#include "msocket.h"
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

MTPSocketEntry *sm;

void print_msocket_entry(MTPSocketEntry entry)
{
    printf("MTP Socket Entry:\n");
    // printf("  is_free: %d\n", entry.is_free);
    printf("  process_id: %d\n", entry.process_id);
    printf("  udp_socket_id: %d\n", entry.udp_socket_id);
    printf("  remote_addr: %s:%d\n", inet_ntoa(entry.remote_addr.sin_addr), ntohs(entry.remote_addr.sin_port));
    printf("  send_buffer:\n");
    for (int i = 0; i < MAX_SEND_BUFFER_SIZE; i++)
    {
        if (entry.send_buffer[i].seq_num == -2)
            continue;
        printf("    [%d] seq_num: %d, data: ", i, entry.send_buffer[i].seq_num);
        for (int j = 0; j < 10; j++)
        {
            printf("%c", entry.send_buffer[i].data[j]);
        }
        printf("\n");
    }
    printf("  recv_buffer:\n");
    for (int i = 0; i < MAX_RECV_BUFFER_SIZE; i++)
    {
        if (entry.recv_buffer[i].seq_num == -2)
            continue;
        printf("    [%d] seq_num: %d, data: ", i, entry.recv_buffer[i].seq_num);
        for (int j = 0; j < 10; j++)
        {
            printf("%c", entry.recv_buffer[i].data[j]);
        }
        printf("\n");
    }
    // printf("  swnd: base = %d, size = %d, seq_nums = [", entry.swnd.base, entry.swnd.size);
    // for (int i = 0; i < MAX_SEQ_NO + 1; i++)
    // {
    //     printf("%d ", entry.swnd.seq_nums[i]);
    // }
    // printf("], last_seq_num = %d\n", entry.swnd.last_seq_num);
    // printf("  rwnd: base = %d, size = %d, seq_nums = [", entry.rwnd.base, entry.rwnd.size);
    // for (int i = 0; i < MAX_SEQ_NO + 1; i++)
    // {
    //     printf("%d ", entry.rwnd.seq_nums[i]);
    // }
    // printf("], last_seq_num = %d\n", entry.rwnd.last_seq_num);
    // printf("  last_send_time: %ld.%09ld\n", entry.last_send_time.tv_sec, entry.last_send_time.tv_nsec);
}

void print_msocket_table(MTPSocketEntry *table)
{
    printf("MTP Socket Table:\n");
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        // printf(" i -> %d , isfree -> %d \n", i, table[i].is_free);
        if (table[i].is_free == 0)
        {
            printf("Entry %d:\n", i);
            print_msocket_entry(table[i]);
            printf("\n");
        }
    }
}

int main()
{
    key_t valkey = ftok(".", 95);
    int adjid = shmget(valkey, MAX_SOCKETS * sizeof(MTPSocketEntry), 0777 | IPC_CREAT);
    sm = (MTPSocketEntry *)shmat(adjid, 0, 0);
    while (1)
    {
        print_msocket_table(sm);
        sleep(2);
    }
    shmdt(sm);
}