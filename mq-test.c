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
    char *text;
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
            printf("\tm_data.text = %s\n", m_data.text);
        }

        free(m_data.text);

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
    char text[100];

    // Set the message queue attributes.
    mqattr.mq_flags = O_NONBLOCK;
    mqattr.mq_maxmsg = 1;
    // The maximum size of the queue is one message, which is just the actual
    // data in the msg structure (not the mtype member). Note that the text
    // member is just a pointer to a string. The size of the message only
    // includes the size of the pointer. The data the text pointer points
    // to needs to be a new copy of the original data for this to work,
    // otherwise the main thread could modify that memory while the stats
    // thread is working on it. This works because the stats thread can see
    // that copy on the heap too.
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

        // Set the text string to something and create a copy of it for the
        // message.
        if (snprintf(text, 6, "Shaun") < 0) {
            fprintf(stderr, "snprintf error");
            mq_unlink(queue);
            exit(EXIT_FAILURE);
        }

        if ((m_data.text = malloc(strlen(text) + 1)) == NULL) {
            perror("malloc error");
            mq_unlink(queue);
            exit(EXIT_FAILURE);
        }

        memcpy(m_data.text, text, strlen(text) + 1);
        m_data.bytes = (uint64_t)count;
        // The shutdown notification is done in the message itself. We need
        // to ensure that the stats thread gets the last update before we
        // exit the main thread. Here we indicate not to shutdown.
        m_data.shutdown = false;
        // Change some stuff to ensure it's not changed in the message.
        data[0] = 1;
        data[1] = 2;
        text[0] = 'Z';

        // If we would block or the queue is full, we continue processing
        // data and send later.
        if (mq_send(mqdes, (const char *)&m_data, sizeof(m_data), 1) != 0) {
            if (errno != EAGAIN) {
                perror("mq_send error");
                mq_unlink(queue);
                exit(EXIT_FAILURE);
            } else {
                // Free the text data copy and try again next time.
                free(m_data.text);
            }
        } else {
            printf("Sent message to stats thread.\n");
        }

        count++;
    }

    // Prepare the final stats message.
    data[0] = 3;
    data[1] = 4;
    memcpy(m_data.numbers, data, sizeof(data));

    // Set the text string to something and create a copy of it for the
    // message.
    if (snprintf(text, 4, "End") < 0) {
        fprintf(stderr, "snprintf error");
        mq_unlink(queue);
        exit(EXIT_FAILURE);
    }

    if ((m_data.text = malloc(strlen(text) + 1)) == NULL) {
        perror("malloc error");
        mq_unlink(queue);
        exit(EXIT_FAILURE);
    }

    memcpy(m_data.text, text, strlen(text) + 1);
    m_data.bytes = (uint64_t)count;
    m_data.shutdown = true;

    // The stats thread needs to get the last update. Here we indicate a
    // shutdown and wait for the stats thread to get the message before
    // joining its thread.
    while (true) {
        // Send the last stats update and indicate we need to shutdown. Once
        // sent, we wait to join the stats thread. If we would block or the
        // queue is full, we try again.
        if (mq_send(mqdes, (const char *)&m_data, sizeof(m_data), 1) != 0) {
            if (errno != EAGAIN) {
                perror("mq_send error");
                mq_unlink(queue);
                exit(EXIT_FAILURE);
            }
        } else {
            printf("Sent last stats and shutdown flag.\n");
            break;
        }
    }

    pthread_join(stat_thread, NULL);
    mq_unlink(queue);
    exit(EXIT_SUCCESS);
}

