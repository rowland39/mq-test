#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mqueue.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>

const char *queue = "/test_stats_queue";

typedef struct {
    uint64_t bytes;
    int numbers[2];
    bool shutdown;
} msg_data;

#define MAX_COUNT   1000000

void *
print_stats(void *arg)
{
    mqd_t mqdes;
    msg_data m_data;

    // The other queue attributes were set when it was created. We open it
    // with O_NONBLOCK explicitly in this case however, otherwise it would
    // be opened with blocking.
    if ((mqdes = mq_open(queue, O_RDONLY|O_NONBLOCK)) == (mqd_t) -1) {
        perror("\tmq_open error");
        exit(EXIT_FAILURE);
    }

    while (true) {
        usleep(250);

        if (mq_receive(mqdes, (char *)&m_data, sizeof(m_data), NULL) == -1) {
            if (errno == EAGAIN) {
                printf("\tmq_receive would have blocked.\n");
                continue;
            }

            perror("mq_receive error");
            exit(EXIT_FAILURE);
        } else {
            printf("\tm_data.bytes = %"PRIu64"\n", m_data.bytes);
            printf("\tm_data.numbers[0] = %d\n", m_data.numbers[0]);
            printf("\tm_data.numbers[1] = %d\n", m_data.numbers[1]);
        }

        if (m_data.shutdown) {
            printf("\tShutdown message received.\n");
            return NULL;
        }
    }
}

int
main(void)
{
    mqd_t mqdes;
    struct mq_attr mqattr;
    msg_data m_data;
    pthread_t stat_thread;
    int count = 0;
    int data[2];

    // Set the message queue attributes.
    mqattr.mq_flags = O_NONBLOCK;
    mqattr.mq_maxmsg = 1;
    mqattr.mq_msgsize = sizeof(msg_data);
    mqattr.mq_curmsgs = 0;

    // Even tough mq_flags is set to O_NONBLOCK, we need to add that to the
    // flags explicitly. The main point of the previous settings in mqattr
    // was to set the number of messages allowed and the message size.
    if ((mqdes = mq_open(queue, O_WRONLY|O_NONBLOCK|O_CREAT, 0600, &mqattr))
        == (mqd_t) -1) {
        perror("mq_open error");
        exit(EXIT_FAILURE);
    }

    // Start the stats thread.
    if (pthread_create(&stat_thread, NULL, print_stats, NULL) == -1) {
        perror("unable to create the stats thread");
        mq_unlink(queue);
        exit(EXIT_FAILURE);
    }

    while (count < 1000000) {
        // Do stuff.
        data[0] = 0;
        data[1] = 1;
        memcpy(m_data.numbers, data, sizeof(data));
        m_data.bytes = (uint64_t)count;
        // The shutdown notification is done in the message itself. We need
        // to ensure that the stats thread gets the last update before we
        // exit the main thread. Here we indicate not to shutdown.
        m_data.shutdown = false;
        // Change some stuff to ensure it's not changed in the message.
        data[0] = 1;
        data[1] = 2;

        // if we would block or the queue is full, we continue processing
        // data and send later.
        if (mq_send(mqdes, (const char *)&m_data, sizeof(m_data), 1) != 0) {
            if (errno != EAGAIN) {
                perror("mq_send error");
                mq_unlink(queue);
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
        memcpy(m_data.numbers, data, sizeof(data));
        m_data.bytes = (uint64_t)count;
        m_data.shutdown = true;

        // Send the last stats update and indicate we need to shutdown. Once
        // sent, we wait to join the stats thread. If we would block or the
        // queue is full, we continue and try again later.
        if (mq_send(mqdes, (const char *)&m_data, sizeof(m_data), 1) != 0) {
            if (errno == EAGAIN) {
                continue;
            }

            perror("mq_send error");
            mq_unlink(queue);
            exit(EXIT_FAILURE);
        } else {
            printf("Sent last stats and shutdown flag.\n");
            break;
        }
    }

    pthread_join(stat_thread, NULL);
    mq_unlink(queue);
    exit(EXIT_SUCCESS);
}

