#define LOG_TAG "MmxUtil"
/*
#undef NDEBUG 
#define LOG_NDEBUG   0   //LOGV
#define LOG_NIDEBUG  0   //LOGI
#define LOG_NDDEBUG 0    //LOGD
#define LOG_NEDEBUG 0    //LOGD
*/
#include <utils/Log.h>

#include "MmxUtil.h"
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>

namespace android {

static sVRDMA_CBLK f_cblk;
//data descriptor
sSX_DESC* MmxUtil::sx_desc_get()
{
    sSX_DESC *desc = (sSX_DESC*)malloc(sizeof(sSX_DESC));
    CHECK(desc != NULL);

    desc->data      = NULL;
    desc->data_len  = 0;
    desc->next      = NULL;

    return desc;
}

void MmxUtil::sx_desc_put(sSX_DESC  *desc)
{
     sSX_DESC  *curr;
     sSX_DESC  *next;


     // Consistency check.
     CHECK(desc != NULL);

     // Initialize current.
     curr = desc;

     // Release descriptors and data.
     while(curr != NULL)
     {
         next = curr->next;

         free(curr->data);
         free(curr);

         curr = next;
     }
}

void MmxUtil::sx_desc_put2(sSX_DESC  *desc)
{
    sSX_DESC  *curr;
    sSX_DESC  *next;
    // Consistency check.

    CHECK(desc != NULL);
    // Initialize current.
    curr = desc;
    // Release descriptors and data.
    while(curr != NULL)
    {
        next = curr->next;
        //free(curr->data);
        free(curr);
        curr = next;
    }
}

//pipe
void MmxUtil::sx_pipe_init()
{
    UINT32  i;


    for(i = 0; i < SX_VRDMA_MAX; i++)
    {
        // Create queue.
        f_cblk.queue[i] = sx_queue_create();
    }
}


void MmxUtil::sx_pipe_put(UINT32 index, void *data)
{
    sx_queue_push(f_cblk.queue[index], data);
}


void * MmxUtil::sx_pipe_get(UINT32  index)
{
    return sx_queue_pull(f_cblk.queue[index]);
}


unsigned int MmxUtil::sx_pipe_len_get(UINT32  index)
{
    return sx_queue_len_get(f_cblk.queue[index]);
}

//queue
SX_QUEUE MmxUtil::sx_queue_create()
{
    sQUEUE *queue = (sQUEUE*)malloc(sizeof(sQUEUE));

    pthread_mutex_init(&queue->lock, NULL);

    sem_init(&queue->sem, 0, 0);

    queue->head = NULL;
    queue->tail = NULL;
    queue->len  = 0;

    return queue;
}


// --------------------------------------------------------
// rt_queue_destroy
//      Destory a queue.
//
void MmxUtil::sx_queue_destroy(SX_QUEUE queue_id)
{
    sQUEUE *queue;
    sNODE  *curr;
    sNODE  *next;


    // Get the queue.
    queue = (sQUEUE*)queue_id;

    // Free every node.
    curr = queue->head;
    while(curr)
    {
        next = curr->next;

        // Free data.
        free(curr->data);

        // Free node.
        free(curr);

        curr = next;
    }

    // Destroy mutex.
    pthread_mutex_destroy(&queue->lock);

    // Free queue.
    free(queue);
}


// --------------------------------------------------------
// rt_queue_push
//      Push a data payload.
//
void MmxUtil::sx_queue_push(SX_QUEUE queue_id, void *data)
{
    sNODE  *node;
    sQUEUE *queue;


    CHECK(data != NULL);

    // Get queue.
    queue = (sQUEUE*)queue_id;

    // Construct new node.
    node = (sNODE*)malloc(sizeof(sNODE));
    node->data = data;
    node->next = NULL;

    // Lock.
    pthread_mutex_lock(&queue->lock);

#if 0
    printf("(rt_queue): push(): id = 0x%x, len = %d\n",
            (int) queue_id,
            queue->len);
#endif

    // Is list empty?
    if(queue->head == NULL)
    {
        // List is empty, insert only node.
        queue->head = node;

        queue->tail = node;

        goto cleanup;
    }

    // Append to end.
    queue->tail->next = node;

    // Update tail.
    queue->tail = node;

cleanup:

    queue->len++;

//    if(queue->len > queue->high_water_mark)
//    {
//        printf("(rt_queue): push(): id = 0x%x, len = %d\n",
//               queue_id,
//               queue->len);
//
//        queue->high_water_mark = queue->len;
//    }

    pthread_mutex_unlock(&queue->lock);

//    sem_post(&queue->sem);
}


// --------------------------------------------------------
// rt_queue_pull
//      Pull a data payload.
//
void * MmxUtil::sx_queue_pull(SX_QUEUE queue_id)
{
    sQUEUE         *queue;
    unsigned char  *data;
    sNODE          *next;

    // Get queue.
    queue = (sQUEUE*)queue_id;

//    sem_wait(&queue->sem);

    // Lock.
    pthread_mutex_lock(&queue->lock);

#if 0
    printf("(rt_queue): pull(): id = 0x%x, len = %d\n",
            (int) queue,
            queue->len);
#endif

    // Is list empty?
    if(queue->head == NULL)
    {
        CHECK(queue->tail == NULL);

        // List is empty.
        data = NULL;

        goto cleanup;
    }

    if(queue->head == queue->tail)
    {
        CHECK(queue->head->next == NULL);
        CHECK(queue->tail->next == NULL);

        // List only has one element.
        data = (unsigned char*)queue->head->data;

        // Free the node.
        free(queue->head);

        // Reset head and tail.
        queue->head = NULL;
        queue->tail = NULL;

        goto cleanup;
    }

    // More than one element.

    // Return packet.
    data = (unsigned char*)queue->head->data;

    // Get next.
    next = queue->head->next;

    // Free head.
    free(queue->head);

    // Advance head.
    queue->head = next;

cleanup:

    if(data != NULL)
    {
        queue->len--;
    }

    pthread_mutex_unlock(&queue->lock);

    return (void*)data;
}


unsigned int MmxUtil::sx_queue_len_get(SX_QUEUE queue_id)
{
    sQUEUE         *queue;
    unsigned int    len;


    // Get queue.
    queue = (sQUEUE*)queue_id;

    pthread_mutex_lock(&queue->lock);

    len = queue->len;

    pthread_mutex_unlock(&queue->lock);

    return len;
}

} // namespace android