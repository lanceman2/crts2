#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <atomic>
#include <map>
#include <list>
#include <stack>

#include "crts/debug.h"
#include "crts/Filter.hpp" // CRTSFilter user module interface
#include "FilterModule.hpp" // opaque co-class
#include "Stream.hpp"
#include "pthread_wrappers.h"


const uint32_t CRTSFilter::ALL_CHANNELS = (uint32_t) -1;


CRTSStream::CRTSStream(std::atomic<bool> &isRunning_in):
    isRunning(isRunning_in)
{

}


//const uint32_t CRTSFilter::defaultBufferQueueLength = 3;

FilterModule::FilterModule(Stream *stream, CRTSFilter *filter_in,
        void *(*destroyFilter_in)(CRTSFilter *), int32_t loadIndex_in,
        std::string name_in):
    filter(filter_in),
    destroyFilter(destroyFilter_in),
    loadIndex(loadIndex_in),
    readers(0), writers(0), readerIndexes(0),
    numReaders(0), numWriters(0), name(name_in),
    threadGroup(0)
    //, bufferQueueLength(CRTSFilter::defaultBufferQueueLength)
{
    // We will reuse filter->filterModule after using it
    // as the bool canWriteBufferIn:
    canWriteBufferIn = (filter->filterModule)?true:false;
    filter->filterModule = this;

    filter->stream = new CRTSStream(stream->isRunning);
    name += "(";
    name += std::to_string(loadIndex);
    name += ")";
    DSPEW();
}


FilterModule::~FilterModule(void)
{
    

    DSPEW();

    // TODO: take down connections

    // TODO: free memory from realloc()
}


// Filters, by default, do not know if they run as a single separate
// thread, of with a group filters that share a thread.  That is decided
// from the thing that starts the scenario which runs the program crts_radio
// via command-line arguments, or other high level interface.  We call it
// filter thread (process) partitioning.
//
// TODO: Extend thread partitioning to thread and process partitioning.
//
// This buffer pool thing is so we can pass buffers between any number of
// filters.  By not having this memory on the stack we enable the filter
// to be able to past this buffer from one thread filter to another, and
// so on down the line.
//
// A filter can choose to reuse and pass through a buffer that was passed
// to it from a previous adjust filter, or it can add another buffer to
// pass up stream using the first buffer as just an input buffer.
//
// So ya, cases are:
//
//   1.  through away the box (buffer); really we recycle it; or
//
//   2.  repackage the data using the same box (buffer)
//
//
#ifdef DEBUG
// TODO: pick a better magic salt
#  define MAGIC ((uint64_t) 1224979098644774913)
#endif



// TODO: Figure out the how to do the simplified case when the
// mutex and conditional is not needed and the filter we write
// is in the same thread.
//
// TODO: Figure out how to seamlessly, from the filter writers
// prospective, do the inter-process filter write case.

// We make a buffer that adds extra header to the top of it.


struct Header
{
#ifdef DEBUG
    uint64_t magic;
#endif

    // If there are threadGroups then this
    // mutex lock is held by the thread calling CTRSFilter::write().
    pthread_mutex_t mutex;

    // Then useCount drops to zero we recycle this buffer.  useCount is
    // used in a multi-threaded version of reference counting.  Since this
    // struct is only defined in this file, you can follow the use of this
    // useCount in just this file.
    std::atomic<uint32_t> useCount;
    size_t len; // is constant after being created
};


// A struct with added memory to the bottom of it.
struct Buffer
{
    struct Header header; // header must be first in struct

    // This pointer should stay aligned so we can offset
    // to this pointer and set this pointer to any struct
    // or array that we want.

    /* think struct padding HERE */

    uint8_t ptr[1]; // just a pointer that already has 1 byte
    // plus the memory below this struct.  The compiler guarantees that
    // ptr is memory aligned because it is 1 byte in a structure.

    /* think padding HERE TOO */
};

// Size of the buffer with added x bytes of memory.
#define BUFFER_SIZE(x)  (sizeof(struct Buffer) + (x-1))

// To access the top of the buffer from the ptr pointer.
#define BUFFER_HEADER(ptr)\
    ((struct Header*)\
        (\
            ((uint8_t*) ptr) \
                - sizeof(struct Header)\
        )\
    )

// Pointer to ptr in struct Buffer
#define BUFFER_PTR(top)\
    ((void*) (((struct Buffer *) top)->ptr))


// This must be thread safe.
static void freeBuffer(struct Header *h)
{
    DASSERT(h, "");
#ifdef DEBUG
    DASSERT(h->magic == MAGIC, "Bad memory pointer");
    DASSERT(h->len > 0, "");
    h->magic = 0;
    memset(h, 0, h->len);
#endif

    // Making sure that buffers are cleaned up.
    // This will spew too much to leave uncommented.
    //WARN("freeing buffer=%p", h);

    free(h);
}


#if 0
void CRTSFilter::setBufferQueueLength(uint32_t n)
{
    filterModule->bufferQueueLength = n;
}
#endif



static void *filterThreadWrite(ThreadGroup *threadGroup)
{
    DASSERT(threadGroup, "");

    // Put some constant pointers on the stack.
    pthread_mutex_t* mutex = &(threadGroup->mutex);
    pthread_cond_t* cond = &(threadGroup->cond);

    DASSERT(mutex, "");
    DASSERT(cond, "");


    // These variables will change at every loop:
    FilterModule *filterModule;
    void *buffer;
    size_t len;
    uint32_t channelNum;


    // mutex limits access to all the data in ThreadGroup starting at
    // ThreadGroup::filterModule in the class declaration, which can
    // and will be changed between loops.
    //
    MUTEX_LOCK(mutex);
    DSPEW("thread %" PRIu32 " starting", threadGroup->threadNum);


    // Now that we have the threads mutex lock we can wait for all
    // the threads and the main thread to be in an "initialized" state.
    //
    // This BARRIER stops the main thread from queuing up thread read
    // events before we have the threads ready to receive them via
    // the pointer, threadGroup->filterModule.
    BARRIER_WAIT(threadGroup->barrier);

    // We can't have a request yet, while we hold the lock.
    DASSERT(!threadGroup->filterModule, "");

    while(true)
    {
        if(!threadGroup->filterModule)
        {
            threadGroup->threadWaiting = true;
            // Here we will loose the mutex lock and block waiting for a
            // conditional signal.
            //
            // WAITING FOR SIGNAL HERE
            ASSERT((errno = pthread_cond_wait(cond, mutex)) == 0, "");
            // Now we have the mutex lock again.
            //
            // By the time another thread (or this thread) gets this
            // threads mutex lock again this thread will be calling
            // the CTRSFilter::write().
            threadGroup->threadWaiting = false;
        }
        // else
        //    had a filterModule already set before we first got the lock
        //    so we will skip waiting for the signal.  This if/else
        //    overcomes the first loop startup race condition, without the
        //    use of a barrier or something like that.

        // Now we have the mutex lock.

        if(!threadGroup->filterModule)
            // There is no request so this is just a signal to return.
            break;

        // The filter module and what is written may change in each loop.
        DASSERT(threadGroup->filterModule->filter, "");
        filterModule = threadGroup->filterModule;

        // We are a source (no writers) or we where passed a buffer
        DASSERT((filterModule->writers && threadGroup->buffer) ||
                !filterModule->writers, "");
        // Check that the buffer is one of ours.
        DASSERT(!filterModule->writers || (threadGroup->buffer &&
                BUFFER_HEADER(threadGroup->buffer)->magic == MAGIC), "");

        // Receive the orders for this thread.  We need to set local
        // stack variables with the values for this write() request.
        buffer = threadGroup->buffer;
        len = threadGroup->len;
        channelNum = threadGroup->channelNum;

        // If another thread wants to know, they can look at this pointer to
        // see this thread is doing its next writes, and we mark that we
        // are ready to queue up the next request if we continue to loop.
        threadGroup->filterModule = 0;


        MUTEX_UNLOCK(mutex);

        // While this thread it carrying out its' orders new orders
        // may be set, queued up, by another thread.
        //
        // This may be a time consuming call.

        filterModule->filter->write(buffer, len, channelNum);


        MUTEX_LOCK(mutex);

        if(threadGroup->queueMutex)
        {
            DASSERT(threadGroup->queueCond, "");

            MUTEX_LOCK(threadGroup->queueMutex);

            // We have one in the "queue" from FilterModule::write().
            // There should be a thread waiting, because of this
            // "queuing".
            ASSERT((errno = pthread_cond_signal(
                            threadGroup->queueCond)) == 0, "");

            MUTEX_UNLOCK(threadGroup->queueMutex);

            // And some time later the thread we just signaled will
            // setup for the next CRTSFilter::write() call
            // buffer, len, and channelNum.
        }

        // Remove/free any buffers that the filterModule->filter->write()
        // created that have not been passed to another write() call, or
        // are no longer on a thread write() stack.
        filterModule->removeUnusedBuffers();

        if(buffer)
        {
            // Remove any buffers that the above
            // filterModule->filter->write() did not create and this
            // threads above filterModule->filter->write() just happens to
            // be the last user of.
            struct Header *h = BUFFER_HEADER(buffer);

            if(h->useCount.fetch_sub(1) == 1)
                freeBuffer(h);
        }
    }

    // Let the other threads know that we are done running this thread.
    // If other code sees this as not set than this thread is still
    // running.
    threadGroup->hasReturned = true;

    MUTEX_UNLOCK(mutex);

    DSPEW("thread %" PRIu32 " returning", threadGroup->threadNum);

    return 0;
}


void ThreadGroup::run(void)
{
    //DSPEW("ThreadGroup %" PRIu32 " creating pthread", threadNum);

    filterModule = 0;

    errno = 0;
    ASSERT((errno = pthread_create(&thread, 0/*pthread_attr_t* */ ,
                (void *(*) (void *)) filterThreadWrite,
                (void *) this)) == 0, "");
}


uint32_t ThreadGroup::createCount = 0;
pthread_t ThreadGroup::mainThread = pthread_self();
pthread_barrier_t *ThreadGroup::barrier = 0;


// We should have a write lock on the stream to call this.
// TODO: or add a lock and unlock call to this.
ThreadGroup::ThreadGroup(Stream *stream_in):
    cond(PTHREAD_COND_INITIALIZER),
    mutex(PTHREAD_MUTEX_INITIALIZER),
    threadNum(++createCount),
    stream(*stream_in),
    hasReturned(false),
    filterModule(0)
{
    DASSERT(pthread_equal(mainThread, pthread_self()), "");
    // There dam well better be a Stream object,
    DASSERT(stream_in, "");
    // and it better be in a running mode.
    DASSERT(stream.isRunning, "");
    stream.threadGroups.push_back(this);
    DSPEW("threadGroup %" PRIu32, threadNum);
}


// We should have a write lock on the stream to call this.
// TODO: or add a lock and unlock call to this.
ThreadGroup::~ThreadGroup()
{
    DASSERT(pthread_equal(mainThread, pthread_self()), "");
    // We better be in stream shutdown mode.
    // TODO: until we make threads more dynamic.
    DASSERT(!stream.isRunning, "");

    MUTEX_LOCK(&mutex);
    ASSERT((errno = pthread_cond_signal(&cond)) == 0, "");
    MUTEX_UNLOCK(&mutex);
    
    DSPEW("waiting for thread %" PRIu32 " to join", threadNum);

    ASSERT((errno = pthread_join(thread, 0/*void **retval */) == 0), "");

    // remove this object from the list.    
    DSPEW("ThreadGroup thread %" PRIu32 " joined", threadNum);
    stream.threadGroups.remove(this);
}


CRTSFilter::~CRTSFilter(void) { DSPEW(); }


CRTSFilter::CRTSFilter(bool canWriteBufferIn):
    // We use this pointer variable as a flag before we use it to be the
    // pointer to the Filtermodule, just so we do not have to declare
    // another variable in CRTSFilter.  See FilterModule::FilterModule().
    filterModule(canWriteBufferIn?((FilterModule*) 1/*nonzero*/):0)
{
    DSPEW("canWriteBufferIn=%d", canWriteBufferIn);
}


void CRTSFilter::writePush(void *buffer, size_t bufferLen,
        uint32_t channelNum)
{
    DASSERT(buffer, "");
    DASSERT(bufferLen, "");

    // channelNum must be a reader channel in this filter
    DASSERT(filterModule->numReaders > channelNum ||
            channelNum == ALL_CHANNELS,
            "!(filterModule->numReaders=%" PRIu32
            " > channelNum=%" PRIu32 ")",
            filterModule->numReaders, channelNum);
    // the reader must have this filter as a writer channel
    DASSERT(channelNum == ALL_CHANNELS ||
            filterModule->readers[channelNum]->numWriters >
            filterModule->readerIndexes[channelNum],
            "!(reader numWriters %" PRIu32
            " > reader channel %" PRIu32 ")",
            filterModule->readers[channelNum]->numWriters,
            filterModule->readerIndexes[channelNum]);

    //
    // TODO: Add write failure mode .........
    //

    FilterModule *to;

    if(channelNum != ALL_CHANNELS)
    {
        // This filter writes to the connected reader filter at
        // the channel index channelNum.
        to = filterModule->readers[channelNum];

        to->write(buffer, bufferLen,
                filterModule->readerIndexes[channelNum],
                // Is this writing to a different thread?
                (to->threadGroup != filterModule->threadGroup)?
                true: false);
    }
    else
        // Write to all readers that we have.
        for(uint32_t i=0; i < filterModule->numReaders; ++i)
        {
            to = filterModule->readers[i];
            // This filter writes to the connected reader filter at
            // the channel index channelNum.
            to->write(buffer, bufferLen,
                    filterModule->readerIndexes[i],
                    // Is this writing to a different thread?
                    (to->threadGroup != filterModule->threadGroup)?
                    true: false);
        }
}



// This is called in CTRSFilter::write() to create a new buffer
// that is automatically cleaned up at the end of it's use.
void *CRTSFilter::getBuffer(size_t bufferLen)
{
    struct Buffer *buf = (struct Buffer *) malloc(BUFFER_SIZE(bufferLen));
    ASSERT(buf, "malloc() failed");
#ifdef DEBUG
    memset(buf, 0, bufferLen);
    ((struct Header*) buf)->magic = MAGIC;
#endif
    buf->header.len = bufferLen;
    buf->header.useCount = 1;
    // The buffer in this list will be popped at the end of the
    // FilterModule::write() call.  It will be freed if it is not passed
    // to another FilterModule::write() from within modules
    // CRTSFilter::write() call; otherwise if is freed in the last
    // FilterModule::write() in a stack of FilterModule::write() calls.
    this->filterModule->buffers.push(buf);

    return BUFFER_PTR(buf);
}


void CRTSFilter::releaseBuffer(void *buffer)
{
    // This could be a useful for decrementing the useCount of a buffer in
    // the CRTSFilter::write() call to alleviate buffer access contention.
    // If this is not called the buffer useCount is automatically
    // decremented in FilterModule::write() that called the
    // CRTSFilter::write();
    //
    // TODO: WRITE THIS FUNCTION  This may require adding move meta variables
    // to the buffer.

}



// The buffer used here must be from this 
// This checks the buffers and calls the underlying filter writers
// CRTSFilter::write()
//
// CAUTION: This code is super tricky.  If do not understand
// multi-threaded code, go home.
//
void FilterModule::write(void *buffer, size_t len, uint32_t channelNum,
        bool toDifferentThread)
{

    struct Header *h = 0;

    if(buffer)
    {
        DASSERT(len > 0, "");
        // TODO: We are assuming the buffer points to the returned pointer
        // from getBuffer() but we need to extend this use to include
        // pointing to any part of the allocated buffer, so the user can
        // use the buffer as they see fit.
        h = BUFFER_HEADER(buffer);

        // Check that the buffer is one of ours.
        DASSERT(h->magic == MAGIC, "");

        // Mark this buffer as in use by this filter.  Once the useCount
        // goes to zero it will never go back up again. 
        ++h->useCount;
    }


    if(toDifferentThread)
    {
        // There must be threadGroups for all FilterModules.
        DASSERT(threadGroup, "");

        // In this case this function is being called from a thread
        // that is not from the threadGroup of this object.

        // We need to increment the buffer useCount for the thread here
        if(h)
        {
            ++h->useCount;
            // That will reserve this buffer for this thread that
            // we will signal to run and that thread will release
            // this by decrementing the useCount when it is done.
        }

        // This is an interesting point in the simulation.  This is where
        // this thread may be blocked waiting of the thread of the "next
        // filter" to finish.

        MUTEX_LOCK(&threadGroup->mutex);

        if(threadGroup->hasReturned)
        {
            DASSERT(!threadGroup->stream.isRunning, "");
            // This just handles the exit race, the main thread just did
            // not read the stream isRunning flag before another thread
            // set it (in CRTFFiler::write()) after it looked.  No
            // problem, we handle that case here.
            DSPEW("Exit race case: thread %" PRIu32 " has returned but we have a "
                    "FilterModule::write() to it", threadGroup->threadNum);
            if(h && h->useCount.fetch_sub(1) == 1)
                // if the value of useCount was 1 than it has gone to 0
                // now.
                freeBuffer(h);
            MUTEX_UNLOCK(&threadGroup->mutex);
            return;
        }


        if(threadGroup->threadWaiting)
            // signal the thread that is waiting now.
            // The flag threadGroup->threadWaiting and the mutex guarantee
            // that the thread is waiting now.
            ASSERT((errno = pthread_cond_signal(&threadGroup->cond))
                    == 0, "");
            // The thread will wake up only after we release the threads
            // mutex lock down below here.
        
        // If (threadGroup->filterModule) then we have a request already
        // and we must wait for the thread to signal us, and then set this
        // request.  This in effect gives us a "write" queue size of
        // one, making us block when the queue is "full".  This will allow
        // two "adjacent" CRTSFilter write threads to run at the same time.
        //
        // Ya, seamless parallel processing: the parallelization happens
        // from the program runners option, not from the CRTSFilter
        // modules code.
        //
        if(threadGroup->filterModule)
        {
            // This 
            pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
            pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

            // This signal wait call is very important.  Without it we
            // could end up having a fast CRTSFilter overrunning its
            // slower adjacent CRTSFilter.
            //
            // Note: we have two interlocking mutexes.

            MUTEX_LOCK(&mutex);

            // Let the threadGroup thread know about these:
            threadGroup->queueMutex = &mutex;
            threadGroup->queueCond = &cond;

            MUTEX_UNLOCK(&threadGroup->mutex);

            // From the threadGroup->threadWaiting the state of the
            // threadGroup thread is guaranteed to become running
            // in the 

            // We release the mutex lock and wait:
            ASSERT((errno = pthread_cond_wait(&cond, &mutex)) == 0, "");
            // Now we have the mutex lock again.

            MUTEX_LOCK(&threadGroup->mutex);

            MUTEX_UNLOCK(&mutex);

            // There may be kernel/system resources associated with
            // these, so we destroy them.
            ASSERT((errno = pthread_mutex_destroy(&mutex)) == 0, "");
            ASSERT((errno = pthread_cond_destroy(&cond)) == 0, "");

            // Mark that there is nothing in the threadGroup queue
            threadGroup->queueMutex = 0;
#ifdef DEBUG
            // The logic does not require that this be reset to zero
            // but resetting it will help check/catch an assertion.
            threadGroup->queueCond = 0;
#endif
        }

        DASSERT(!threadGroup->filterModule, "");

        threadGroup->filterModule = this;
        threadGroup->buffer = buffer;
        threadGroup->len = len;
        threadGroup->channelNum = channelNum;

        MUTEX_UNLOCK(&threadGroup->mutex);
        // The thread threadGroup will decrement the buffer use count at
        // the end of it's cycle.
    }
    else
    {
        // This is being called from a stack of more than one
        // CRTSFilter::write() calls so there is no need to get a mutex
        // lock, the current thread stack keeps CRTSFilter::write()
        // function calls in a sequence.
        //
        // The CRTSFilter::write() call can generate more
        // FilterModule::writes() via module writer interface
        // CRTSFilter::writePush().
        this->filter->write(buffer, len, channelNum);

        removeUnusedBuffers();
    }

    if(h && h->useCount.fetch_sub(1) == 1)
        // header from the buffer passed in.
        freeBuffer(h);
}


void FilterModule::removeUnusedBuffers(void)
{
    while(!buffers.empty())
    {
        // In this block we are checking if we have any buffers
        // created in this->filter->write() that are not in use
        // now.  If they are in use they will get freed in a
        // different thread after the thread returns from a
        // CRTSFilter::write() call.

        struct Header *header = (struct Header *) buffers.top();

        if(header->useCount.fetch_sub(1) == 1)
            freeBuffer(header);
        // else this buffer is being used in a filter in
        // another thread.
        buffers.pop();
    }
}

