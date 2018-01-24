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

    // Then useCount drops to zero we recycle this buffer.
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


static void *filterThreadWrite(ThreadGroup *threadGroup)
{
    DASSERT(threadGroup, "");

    // Put some pointers on the stack.
    pthread_mutex_t* mutex = &(threadGroup->mutex);
    pthread_cond_t* cond = &(threadGroup->cond);

    DASSERT(mutex, "");
    DASSERT(cond, "");

    std::atomic<bool> &isRunning = threadGroup->stream.isRunning;

    FilterModule *filterModule;

    DSPEW();

 
    // mutex limits access to all the data in ThreadGroup starting at
    // ThreadGroup::filterModule in the class declaration, which can
    // and will be changed between loops.
    //
    MUTEX_LOCK(mutex);


    while(isRunning)
    {
        if(!threadGroup->filterModule)
            // Here we will loose the mutex lock and block waiting for a
            // conditional signal.
            //
            // WAITING FOR SIGNAL HERE
            ASSERT((errno = pthread_cond_wait(cond, mutex)) == 0, "");
            // Now we have the mutex lock again.
        // else
        //    had a filterModule already set before we first got the lock
        //    so we will skip waiting for the signal.  This if/else
        //    overcomes the first loop startup race condition, without the
        //    use of a barrier or something like that.


        // Now we have the mutex lock.

        // The filter module and what is written will change in each loop.
        DASSERT(threadGroup->filterModule, "");
        DASSERT(threadGroup->filterModule->filter, "");
        filterModule = threadGroup->filterModule;

        // We are a source (no writers) or we where passed a buffer
        DASSERT((filterModule->writers && threadGroup->buffer) ||
                !filterModule->writers, "");
        // Check that the buffer is one of ours.
        DASSERT(!filterModule->writers ||
                BUFFER_HEADER(threadGroup->buffer)->magic == MAGIC, "");

#ifdef DEBUG
        if(!isRunning)
            DSPEW("threadGroup->filterModule \"%s\""
                    " finishing last write cycle",
                    filterModule->name);
#endif

        filterModule->filter->write(
                threadGroup->buffer,
                threadGroup->len,
                threadGroup->channelNum);

#ifdef DEBUG
        if(!isRunning)
            DSPEW("threadGroup->filterModule \"%s\""
                    " finished last write cycle",
                    filterModule->name);
#endif

        // If something wants to know, they can look at this pointer to
        // see this thread is done with it's writes, and we mark that we
        // are ready to wait for the next signal if we continue to loop.
        threadGroup->filterModule = 0;
    }

    MUTEX_UNLOCK(mutex);

    DSPEW("filter \"%s\" thread returning", filterModule->name.c_str());

    return 0;
}


void ThreadGroup::run(void)
{
    DSPEW("Creating writer thread");

    filterModule = 0;

    errno = 0;
    ASSERT((errno = pthread_create(&thread, 0/*pthread_attr_t* */ ,
                (void *(*) (void *)) filterThreadWrite,
                (void *) this)) == 0, "");
}


uint32_t ThreadGroup::createCount = 0;
pthread_t ThreadGroup::mainThread = pthread_self();


// We should have a write lock on the stream to call this.
// TODO: or add a lock and unlock call to this.
ThreadGroup::ThreadGroup(Stream *stream_in):
    cond(PTHREAD_COND_INITIALIZER),
    mutex(PTHREAD_MUTEX_INITIALIZER),
    threadNum(++createCount),
    stream(*stream_in),
    filterModule(0)
{
    DASSERT(pthread_equal(mainThread, pthread_self()), "");
    // There dam well better be a Stream object,
    DASSERT(stream_in, "");
    // and it better be in a running mode.
    DASSERT(stream.isRunning, "");

    // Add this object to the list. 
    stream.threadGroups[this] = this;
    DSPEW();
}


// We should have a write lock on the stream to call this.
// TODO: or add a lock and unlock call to this.
ThreadGroup::~ThreadGroup()
{
    DASSERT(pthread_equal(mainThread, pthread_self()), "");
    // We better be in stream shutdown mode.
    // TODO: until we make threads more dynamic.
    DASSERT(!stream.isRunning, "");

    ASSERT((errno = pthread_join(thread, 0/*void **retval */) == 0), "");

    // remove this object from the list.    
    stream.threadGroups.erase(this);
    DSPEW("thread joined");
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
    this->filterModule->buffers.push(buf);

    return BUFFER_PTR(buf);
}


void CRTSFilter::releaseBuffer(void *buffer)
{
    // TODO: WRITE THIS FUNCTION

}


static void freeBuffer(struct Header *h)
{
    DASSERT(h, "");
#ifdef DEBUG
    DASSERT(h->magic == MAGIC, "Bad memory pointer");
    DASSERT(h->len > 0, "");
    h->magic = 0;
    memset(h, 0, h->len);
#endif

    //WARN("freeing buffer=%p", h);

    free(h);
}


#if 0
void CRTSFilter::setBufferQueueLength(uint32_t n)
{
    filterModule->bufferQueueLength = n;
}
#endif




// The buffer used here must be from this 
// This checks the buffers and calls the underlying filter writers
// CRTSFilter::write()
void FilterModule::write(void *buffer, size_t len, uint32_t channelNum,
        bool toDifferentThread)
{

    // TODO: this code will generate and use threads,
    // and make the buffer thread safe.

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

        // Mark this buffer as in use by this filter.
        ++h->useCount;
    }


    if(threadGroup && toDifferentThread)
    {
        MUTEX_LOCK(&threadGroup->mutex);
        threadGroup->filterModule = this;
        threadGroup->buffer = buffer;
        threadGroup->len = len;
        threadGroup->channelNum = channelNum;
        ASSERT((errno = pthread_cond_signal(&threadGroup->cond)) == 0, "");
        MUTEX_UNLOCK(&threadGroup->mutex);
    }
    else
        // The CRTSFilter::write() call can generate more writes() via module
        // writer interface CRTSFilter::writePush().
        this->filter->write(buffer, len, channelNum);

    while(!buffers.empty())
    {
        struct Header *header = (struct Header *) buffers.top();
        uint32_t useCount = --header->useCount;

        //DSPEW("header->useCount = %" PRIu32 , useCount);

        if(useCount == 0)
            freeBuffer(header);
        // else this buffer is being used in a filter in
        // another thread.
        buffers.pop();
    }

    if(h)
    {
        --h->useCount;
        if(h->useCount == 0)
            freeBuffer(h);
    }
}
