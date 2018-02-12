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
#include "FilterModule.hpp" // opaque co-class
#include "Thread.hpp"
#include "Stream.hpp"
#include "Buffer.hpp"


const uint32_t CRTSFilter::ALL_CHANNELS = (uint32_t) -1;


CRTSStream::CRTSStream(std::atomic<bool> &isRunning_in):
    isRunning(isRunning_in)
{

}


//const uint32_t CRTSFilter::defaultBufferQueueLength = 3;

FilterModule::FilterModule(Stream *stream_in, CRTSFilter *filter_in,
        void *(*destroyFilter_in)(CRTSFilter *), int32_t loadIndex_in,
        std::string name_in):
    stream(stream_in),
    filter(filter_in),
    destroyFilter(destroyFilter_in),
    loadIndex(loadIndex_in),
    readers(0), writers(0), readerIndexes(0),
    numReaders(0), numWriters(0), name(name_in),
    thread(0)
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
    // TODO: This destructor just removes the connection to and from
    // this filter module.  Should it try to make new connections to and
    // from the remaining filter modules?

    // In here we'll handle the editing of the reader and writer lists
    // for both this object and the reader and writer objects.
    //
    // TODO: should the reader and writer arrays be turned into std::maps
    // or something else that's easier to connect and disconnect.
    //
    // TODO: We leave the channel numbers in the CRTSFilter::write() the
    // same.  These channel numbers started out in sequential order as
    // connections are added and are keep in the uint32_t
    // FilterModule::readerIndexes[] for the FilterModule writer to
    // write to the reader with that channel number.
    //
    // So if a FilterModule is removed the channel that received writes
    // from it will no longer happen, leaving a gap in sequence of
    // channels numbers that a reader FilterModule receives from; or
    // should we remove this gap and shift all the channel numbers that
    // a reader FilterModule receives from, possibly "freaking out" the
    // reader FilterModule if it was assuming that writer channel numbers
    // stayed in the same correspondence with filter module.
    //
    //
    // TODO: maybe another abstraction layer is needed to handle
    // FilterModule connectivity management like:
    //
    // CRTSFilter::write(void *buffer, size_t len, CRTSConnection *c);
    //
    // CRTSConnection looks like more pain for the user and developer.
    //

    uint32_t i;

    for(i=0; i<numReaders; ++i)
    {
        // For all reader filters that this writes to
        FilterModule *rf = readers[i];

        // Better not connect to ourself.
        DASSERT(rf != this,
                "connected module filter to itself as a reader");

        // Remove this module from all rf->writers
        uint32_t wi; // write index
        for(wi=0; wi < rf->numWriters; ++wi)
        {
            if(rf->writers[wi] == this)
            {
                // Push the writers over 1 in the array
                uint32_t j;
                for(j = wi + 1; j < rf->numWriters; ++j)
                    rf->writers[j-1] = rf->writers[j];
                --rf->numWriters;
                if(rf->numWriters)
                {
                    rf->writers = (FilterModule **)
                        realloc(rf->writers, rf->numWriters *
                                sizeof(FilterModule *));
                    ASSERT(rf->writers, "realloc() failed");
                }
                else
                {
                    free(rf->writers);
                    rf->writers = 0;
                }
            }
        }
    }

    for(i=0; i<numWriters; ++i)
    {
        // For all writer filters that this reads i.e. calls its
        // CRTSFilter::write().
        FilterModule *wf = writers[i];

        // Better not connect to ourself.
        DASSERT(wf != this,
                "connected module filter to itself as writer");

        // Remove this module from all wf->readers
        uint32_t ri; // read index
        for(ri=0; ri < wf->numReaders; ++ri)
        {
            if(wf->readers[ri] == this)
            {
                // Push the readers over 1 in the array
                uint32_t j;
                for(j = ri + 1; j < wf->numReaders; ++j)
                {
                    wf->readers[j-1] = wf->readers[j];
                    wf->readerIndexes[j-1] = wf->readerIndexes[j];
                }
                --wf->numReaders;
                if(wf->numReaders)
                {
                    wf->readers = (FilterModule **)
                        realloc(wf->readers, wf->numReaders *
                                sizeof(FilterModule *));
                    ASSERT(wf->readers, "realloc() failed");
                    wf->readerIndexes = (uint32_t *)
                        realloc(wf->readerIndexes, wf->numReaders *
                                sizeof(uint32_t));
                    ASSERT(wf->readerIndexes, "realloc() failed");
                 }
                else
                {
                    free(wf->readers);
                    wf->readers = 0;
                }
            }
        }
    }


    if(writers)
    {
        free(writers);
        writers = 0;
    }
    if(readers)
    {
        free(readers);
        readers = 0;
    }
    if(readerIndexes)
    {
        free(readerIndexes);
        readerIndexes = 0;
    }

    DASSERT(stream, "");

    DASSERT(filter, "");
    DASSERT(destroyFilter, "");

    if(thread)
    {
        DASSERT(thread->filterModule != this,
            "this filter module has a thread set to call its' write()");
        thread->filterModules.remove(this);
    }

    // Call the CRTSFilter factory destructor function that we got
    // from loading the plugin.
    destroyFilter(filter);

    stream->map.erase(loadIndex);


    DSPEW("deleted filter: \"%s\"", name.c_str());

    // The std::string name is part of the object so is automatically
    // destroyed.
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
        if(filterModule->numReaders == 0) return;

        // This filter writes to the connected reader filter at
        // the channel index channelNum.
        to = filterModule->readers[channelNum];

        to->write(buffer, bufferLen,
                filterModule->readerIndexes[channelNum],
                // Is this writing to a different thread?
                (to->thread != filterModule->thread)?
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
                    (to->thread != filterModule->thread)?
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
    // to the buffer.  One might ask what is the point of this function...

}



// The buffer used here must be from CRTSFilter::getBuffer().
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

        // NOTE: thread is the pthread we are writing to from the current
        // pthread (not thread).
        //

        // There must be threads for all FilterModules.
        DASSERT(thread, "");

        // In this case this function is being called from a thread
        // that is not from the thread of this object.

        // We need to increment the buffer useCount for the thread here
        if(h)
        {
            ++h->useCount;
            // That will reserve this buffer for this thread that
            // we will signal to run and that thread will release
            // this by decrementing the useCount when it is done.
        }

        // This is an interesting point in the simulation.  This is where
        // this thread may be blocked waiting for the thread of the "next
        // filter" to finish.

        MUTEX_LOCK(&thread->mutex);

        if(thread->hasReturned)
        {
            DASSERT(!thread->stream.isRunning, "");
            // This just handles the exit race, the main thread just did
            // not read the stream isRunning flag before another thread
            // set it (in CRTFFiler::write()) after it looked.  No
            // problem, we handle that case here.
            DSPEW("Exit race case: this thread is trying to call "
                    "FilterModule::write() to thread %" PRIu32
                    " but thread %" PRIu32 " has returned already",
                    thread->threadNum, thread->threadNum);
            if(h && h->useCount.fetch_sub(1) == 1)
                // if the value of useCount was 1 than it has gone to 0
                // now.
                freeBuffer(h);
            MUTEX_UNLOCK(&thread->mutex);
            return;
        }


        
        // If (thread->filterModule) then we have a request already
        // and we must wait for the thread to signal us, and then set this
        // request.  This in effect gives us a "write" queue size of
        // one, making us block when the queue is "full".  This will allow
        // two "adjacent" CRTSFilter write threads to run at the same time.
        //
        if(thread->filterModule)
        {
            // This is the case where this thread must block because there
            // is a request for this thread already.

            pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
            pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

            // This signal wait call is very important.  Without it we
            // could end up having this fast CRTSFilter overrunning its
            // slower adjacent CRTSFilter.
            //
            // Note: we have two interlocking mutexes.

            MUTEX_LOCK(&mutex);

            // Let the thread thread know about these:
            thread->queueMutex = &mutex;
            thread->queueCond = &cond;

            MUTEX_UNLOCK(&thread->mutex);


            // We release the mutex lock and wait:
            ASSERT((errno = pthread_cond_wait(&cond, &mutex)) == 0, "");
            // Now we have the mutex lock again.


            MUTEX_LOCK(&thread->mutex);

            MUTEX_UNLOCK(&mutex);

            // There may be kernel/system resources associated with
            // these, so we destroy them.
            ASSERT((errno = pthread_mutex_destroy(&mutex)) == 0, "");
            ASSERT((errno = pthread_cond_destroy(&cond)) == 0, "");

            // Mark that there is nothing in the thread queue
            thread->queueMutex = 0;
#ifdef DEBUG
            // The logic does not require that this be reset to zero
            // but resetting it will help check/catch an assertion.
            thread->queueCond = 0;
#endif
        }

        if(thread->threadWaiting)
            // signal the thread that is waiting now.
            // The flag thread->threadWaiting and the mutex guarantee
            // that the thread is waiting now.
            ASSERT((errno = pthread_cond_signal(&thread->cond))
                    == 0, "");
            // The thread will wake up only after we release the threads
            // mutex lock down below here.

        DASSERT(!thread->filterModule, "thread %" PRIu32,
                thread->threadNum);

        thread->filterModule = this;
        thread->buffer = buffer;
        thread->len = len;
        thread->channelNum = channelNum;

        MUTEX_UNLOCK(&thread->mutex);
        // The thread thread will decrement the buffer use count at
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
