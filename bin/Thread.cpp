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
#include "pthread_wrappers.h"
#include "FilterModule.hpp" // opaque filter co-class
#include "Thread.hpp"
#include "Stream.hpp"
#include "Buffer.hpp"


// This is the pthread_create() callback function.
//
static void *filterThreadWrite(Thread *thread)
{
    DASSERT(thread, "");
    // No CRTSFilter can set the isRunning yet because
    // of the barrier below.
    DASSERT(thread->stream.isRunning, "");

    // Reference to stream isRunning
    std::atomic<bool> &isRunning = thread->stream.isRunning;

    // Put some constant pointers on the stack.
    pthread_mutex_t* mutex = &(thread->mutex);
    pthread_cond_t* cond = &(thread->cond);
    pthread_mutex_t* streamsMutex = &(Stream::mutex);
    pthread_cond_t* streamsCond = &(Stream::cond);

    DASSERT(mutex, "");
    DASSERT(cond, "");
    DASSERT(streamsMutex, "");
    DASSERT(streamsCond, "");


    // These variables will change at every loop:
    FilterModule *filterModule;
    void *buffer;
    size_t len;
    uint32_t channelNum;


    // mutex limits access to all the data in Thread starting at
    // Thread::filterModule in the class declaration, which can
    // and will be changed between loops.
    //
    MUTEX_LOCK(mutex);
    DSPEW("thread %" PRIu32 " starting", thread->threadNum);

    DASSERT(!thread->filterModule, "");


    // Now that we have the threads mutex lock we can wait for all
    // the threads and the main thread to be in an "initialized" state.
    //
    // This BARRIER stops the main thread from queuing up thread read
    // events before we have the threads ready to receive them via
    // the pointer, thread->filterModule.
    DASSERT(thread->barrier, "");

    {
        pthread_barrier_t *barrier = thread->barrier;
        // We zero this before the using the barrier to avoid a race.
        thread->barrier = 0;
        BARRIER_WAIT(barrier);
    }

    // We can't have a request yet, while we hold the lock.
    DASSERT(!thread->filterModule, "");


    while(true)
    {
        if(!thread->filterModule)
        {
            thread->threadWaiting = true;
            // Here we will loose the mutex lock and block waiting for a
            // conditional signal.  At which time the signaler thread
            // should be setting thread->filterModule.
            //
            // WAITING FOR SIGNAL HERE
            ASSERT((errno = pthread_cond_wait(cond, mutex)) == 0, "");
            // Now we have the mutex lock again.
            //
            // By the time another thread (or this thread) gets this
            // threads mutex lock again this thread will be calling
            // the CTRSFilter::write().
            thread->threadWaiting = false;
        }
        // else
        //    had a filterModule already set before we first got the lock
        //    so we will skip waiting for the signal.
        //
        // Now we have the mutex lock.

        if(!thread->filterModule)
            // There is no request so this is just a signal to return.
            break;

        // The filter module and what is written may change in each loop.
        DASSERT(thread->filterModule->filter, "");
        filterModule = thread->filterModule;

        // We are a source (no writers) or we where passed a buffer
        DASSERT((filterModule->writers && thread->buffer) ||
                !filterModule->writers, "");
        // Check that the buffer is one of ours.
        DASSERT(!filterModule->writers || (thread->buffer &&
                BUFFER_HEADER(thread->buffer)->magic == MAGIC), "");

        // Receive the orders for this thread.  We need to set local
        // stack variables with the values for this write() request.
        buffer = thread->buffer;
        len = thread->len;
        channelNum = thread->channelNum;

        // If another thread wants to know, they can look at this pointer to
        // see this thread is doing its next writes, and we mark that we
        // are ready to queue up the next request if we continue to loop.
        thread->filterModule = 0;


        MUTEX_UNLOCK(mutex);

        // While this thread it carrying out its' orders new orders
        // may be set, queued up, by another thread.
        //
        // This may be a time consuming call.
        //
        filterModule->filter->write(buffer, len, channelNum);


        MUTEX_LOCK(mutex);

        if(thread->queueMutex)
        {
            // There is another thread waiting to write.
            //
            DASSERT(thread->queueCond, "");

            MUTEX_LOCK(thread->queueMutex);

            // We have one in the "queue" from FilterModule::write().
            // There should be a thread waiting, because of this
            // "queuing".
            ASSERT((errno = pthread_cond_signal(
                            thread->queueCond)) == 0, "");

            MUTEX_UNLOCK(thread->queueMutex);

            // And some time later the thread we just signaled will
            // setup for the next CRTSFilter::write() call
            // buffer, len, and channelNum.
        }

        if(!isRunning)
        {
            // A call call to filterModule->filter->write() in this
            // or other thread unset isRunning for this stream.

            // We can be a little slow here, because we are now in
            // a shutdown state.
            //
            // The stream is going to be deleted soon.  When it is
            // we will wake up from pthread_cond_wait() above and see
            // that thread->filterModule is not set.
            //
            // The main thread is now waiting for all the thread in
            // the stream to be not busy.
            //
            // Since the thread run asynchronously there could be
            // unwritten data headed to this thread now, just at a
            // different thread now.
            //
    
            MUTEX_LOCK(streamsMutex);
            if(Stream::waiting)
                // The main thread is calling pthread_cond_wait().
                ASSERT((errno = pthread_cond_signal(streamsCond)) == 0, "");
            MUTEX_UNLOCK(streamsMutex);
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
    thread->hasReturned = true;

    MUTEX_UNLOCK(mutex);

    // Now signal the master/main thread.
    MUTEX_LOCK(streamsMutex);
    DASSERT(isRunning == false, "");
    if(Stream::waiting)
        // The main thread is calling pthread_cond_wait().
        ASSERT((errno = pthread_cond_signal(streamsCond)) == 0, "");
    MUTEX_UNLOCK(streamsMutex);
 
    DSPEW("thread %" PRIu32 " finished returning", thread->threadNum);

    return 0;
}


void Thread::launch(pthread_barrier_t *barrier_in)
{
    DASSERT(barrier_in, "");
    //DSPEW("Thread %" PRIu32 " creating pthread", threadNum);

    filterModule = 0;
    barrier = barrier_in;
    hasReturned = false;
    errno = 0;
    ASSERT((errno = pthread_create(&thread, 0/*pthread_attr_t* */,
                (void *(*) (void *)) filterThreadWrite,
                (void *) this)) == 0, "");
}


uint32_t Thread::createCount = 0;
pthread_t Thread::mainThread = pthread_self();
size_t Thread::totalNumThreads = 0;


// We should have a write lock on the stream to call this.
// TODO: or add a lock and unlock call to this.
Thread::Thread(Stream *stream_in):
    cond(PTHREAD_COND_INITIALIZER),
    mutex(PTHREAD_MUTEX_INITIALIZER),
    threadNum(++createCount),
    barrier(0),
    stream(*stream_in),
    hasReturned(false),
    filterModule(0)
{
    ++totalNumThreads;
    DASSERT(pthread_equal(mainThread, pthread_self()), "");
    // There dam well better be a Stream object,
    DASSERT(stream_in, "");
    // and it better be in a running mode.
    DASSERT(stream.isRunning, "");
    stream.threads.push_back(this);
    DSPEW("thread %" PRIu32, threadNum);
}


// We should have a write lock on the stream to call this.
// TODO: or add a lock and unlock call to this.
Thread::~Thread()
{
    DASSERT(pthread_equal(mainThread, pthread_self()), "");
    // We better be in stream shutdown mode.
    // TODO: until we make threads more dynamic.
    DASSERT(!stream.isRunning, "");

    DASSERT(!filterModule, "");

    MUTEX_LOCK(&mutex);

    // This thread is not busy and should be waiting
    // on a pthread_cond_wait()
    ASSERT((errno = pthread_cond_signal(&cond)) == 0, "");

    MUTEX_UNLOCK(&mutex);

    DSPEW("waiting for thread %" PRIu32 " to join", threadNum);

    ASSERT((errno = pthread_join(thread, 0/*void **retval */) == 0), "");

    // remove this object from the list.
    DSPEW("Thread thread %" PRIu32 " joined", threadNum);

    while(filterModules.size())
        // FilterModule::~FilterModule(void) will remove it.
        delete *(filterModules.begin());

    stream.threads.remove(this);
    --totalNumThreads;
}
