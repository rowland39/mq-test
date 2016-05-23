#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>

#define SHARED_KEY_FILE "/tmp/shared-key.txt"
#define SHARED_KEY_ID   2377
#define MAX_COUNT       1000000

typedef struct {
    uint64_t bytes;
    int numbers[2];
    bool shutdown;
} msg_data;

typedef struct {
    long mtype;
    msg_data data;
} message;

key_t shmkey;
int msgid;

void *
print_stats(void *arg)
{
    message msg;

    while (true) {
        usleep(250);

        if (msgrcv(msgid, &msg, sizeof(msg.data), 1, IPC_NOWAIT) == -1) {
            if (errno == ENOMSG) {
                printf("\tmsgrcv no messages on the queue.\n");
                continue;
            }

            perror("msgrcv error");
            exit(EXIT_FAILURE);
        } else {
            printf("\tmsg.data.bytes = %"PRIu64"\n", msg.data.bytes);
            printf("\tmsg.data.numbers[0] = %d\n", msg.data.numbers[0]);
            printf("\tmsg.data.numbers[1] = %d\n", msg.data.numbers[1]);
        }

        if (msg.data.shutdown) {
            printf("\tShutdown message received.\n");
            return NULL;
        }
    }
}

void
remove_queue(int msqid)
{
    if (msgctl(msgid, IPC_RMID, NULL) == -1) {
        perror("msgctl error (remove queue)");
        exit(EXIT_FAILURE);
    }
}

int
main(void)
{
    int fd;
    struct msqid_ds msgattr;
    message msg;
    pthread_t stat_thread;
    int count = 0;
    int data[2];

    if ((fd = open(SHARED_KEY_FILE, O_CREAT|O_RDWR|O_TRUNC, 0600)) == -1) {
        perror("open error");
        exit(EXIT_FAILURE);
    }

    close(fd);

    // The key and id values are global. They are only written to in the main
    // thread, but they can be read by the main and stats threads at the same
    // time.
    if ((shmkey = ftok(SHARED_KEY_FILE, SHARED_KEY_ID)) == -1) {
        perror("ftok error");
        exit(EXIT_FAILURE);
    }

    if ((msgid = msgget(shmkey, IPC_CREAT|0600)) == -1) {
        perror("msgget error");
        exit(EXIT_FAILURE);
    }

    // Get the current message queue attributes and modify the maximum number
    // of bytes allowed on the queue to be one message.
    if (msgctl(msgid, IPC_STAT, &msgattr) == -1) {
        perror("msgctl error (get attr)");
        exit(EXIT_FAILURE);
    }

    // The maximum size of the queue is one message, which is just the actual
    // data in the msg structure (not the mtype member).
    msgattr.msg_qbytes = (msglen_t)sizeof(msg.data);

    if (msgctl(msgid, IPC_SET, &msgattr) == -1) {
        perror("msgctl error (set max queue size)");
        remove_queue(msgid);
        exit(EXIT_FAILURE);
    }

    // Start the stats thread.
    if (pthread_create(&stat_thread, NULL, print_stats, NULL) == -1) {
        perror("unable to create the stats thread");
        remove_queue(msgid);
        exit(EXIT_FAILURE);
    }

    while (count < 1000000) {
        // Do stuff.
        data[0] = 0;
        data[1] = 1;
        msg.mtype = 1;
        memcpy(msg.data.numbers, data, sizeof(data));
        msg.data.bytes = (uint64_t)count;
        // The shutdown notification is done in the message itself. We need
        // to ensure that the stats thread gets the last update before we
        // exit the main thread. Here we indicate not to shutdown.
        msg.data.shutdown = false;
        // Change some stuff to ensure it's not changed in the message.
        data[0] = 1;
        data[1] = 2;

        // if we would block or the queue is full, we continue processing
        // data and send later.
        if (msgsnd(msgid, &msg, sizeof(msg.data), IPC_NOWAIT) != 0) {
            if (errno != EAGAIN) {
                perror("msgsnd error");
                remove_queue(msgid);
                exit(EXIT_FAILURE);
            }
        } else {
            printf("Sent message to stats thread.\n");
        }

        count++;
    }

    // The stats thread needs to get the last update. Here we indicate a
    // shutdown and wait for the stats thread to get the message before
    // joinin its thread.
    while (true) {
        data[0] = 3;
        data[1] = 4;
        msg.mtype = 1;
        memcpy(msg.data.numbers, data, sizeof(data));
        msg.data.bytes = (uint64_t)count;
        msg.data.shutdown = true;

        // Send the last stats update and indicate we need to shutdown. Once
        // sent, we wait to join the stats thread. If we would block or the
        // queue is full, we try again.
        if (msgsnd(msgid, &msg, sizeof(msg.data), IPC_NOWAIT) != 0) {
            if (errno != EAGAIN) {
                perror("msgsnd error");
                remove_queue(msgid);
                exit(EXIT_FAILURE);
            }
        } else {
            printf("Sent last stats and shutdown flag.\n");
            break;
        }
    }

    pthread_join(stat_thread, NULL);
    remove_queue(msgid);
    exit(EXIT_SUCCESS);
}

