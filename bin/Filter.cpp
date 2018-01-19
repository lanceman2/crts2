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
#include "Stream.hpp"
#include "FilterModule.hpp" // opaque co-class


const uint32_t CRTSFilter::ALL_CHANNELS = (uint32_t) -1;


CRTSStream::CRTSStream(std::atomic<bool> &isRunning_in):
    isRunning(isRunning_in)
{

}


FilterModule::FilterModule(Stream *stream, CRTSFilter *filter_in,
        void *(*destroyFilter_in)(CRTSFilter *), int32_t loadIndex_in,
        std::string name_in):
    filter(filter_in),
    destroyFilter(destroyFilter_in),
    loadIndex(loadIndex_in),
    readers(0), writers(0), readerIndexes(0),
    numReaders(0), numWriters(0), name(name_in),
    bufferQueueLength(CRTSFilter::defaultBufferQueueLength)
{
    this->filter->filterModule = this;
    this->filter->stream = new CRTSStream(stream->isRunning);
    DSPEW();
}


FilterModule::~FilterModule(void)
{
    

    DSPEW();

    // TODO: take down connections

    // TODO: free memory from realloc()
}


// TODO: Figure out the how to do the simplified case when the
// mutex and conditional is not needed and the filter we write
// is in the same thread.
//
// TODO: Figure out how to seamlessly, from the filter writers
// prospective, do the inter-process filter write case.

// We make a buffer that adds extra header to the top of it.

// TODO: Header is a lot of memory for nothing if this is not
// a multi-threaded (or multi-process) app.


struct Header
{
#ifdef DEBUG
    uint64_t magic;
#endif
    std::atomic<uint32_t> useCount;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    size_t len;

    /* think padding HERE */
};

// A struct with added memory to the bottom of it.
struct Buffer
{
    struct Header header; // header must be first in struct

    // This pointer should stay aligned so we can offset
    // to this pointer and set this pointer to any struct
    // or array that we want.

    /* think padding HERE */

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


const uint32_t CRTSFilter::defaultBufferQueueLength = 3;


CRTSFilter::~CRTSFilter(void) { DSPEW(); }


CRTSFilter::CRTSFilter()
{
    DSPEW();
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

    if(channelNum != ALL_CHANNELS)
        // This filter writes to the connected reader filter at
        // the channel index channelNum.
        filterModule->readers[channelNum]->write(buffer, bufferLen,
                filterModule->readerIndexes[channelNum]);
    else
        for(uint32_t i=0; i < filterModule->numReaders; ++i)
            // This filter writes to the connected reader filter at
            // the channel index channelNum.
            filterModule->readers[i]->write(buffer, bufferLen,
                    filterModule->readerIndexes[i]);
}


// It's up to the filter to do buffer pass through, or buffer transfer.
//
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
// TODO: port to 32 bit
#  define MAGIC ((uint64_t) 1224979098644774913)
#endif

void *CRTSFilter::getBuffer(size_t bufferLen, bool canReuse)
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


static void releaseBuffer(struct Header *h)
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


void CRTSFilter::setBufferQueueLength(uint32_t n)
{
    filterModule->bufferQueueLength = n;
}


// The buffer used here must be from this 
// This checks the buffers and calls the underlying filter writers
// CRTSFilter::write()
void FilterModule::write(void *buffer, size_t len, uint32_t channelNum)
{

    // TODO: this code will generate and use threads,
    // and make the buffer thread safe.

    struct Header *h = 0;

    if(buffer)
    {
        DASSERT(len > 0, "");
        h = BUFFER_HEADER(buffer);
        // Mark this buffer as in use by this filter.
        ++h->useCount;
    }

    // The write call can generate more writes() via module writer
    // interface CRTSFilter::writePush().  Call the CRTSFilter::write();
    this->filter->write(buffer, len, channelNum);

    while(!buffers.empty())
    {
        struct Header *header = (struct Header *) buffers.top();
        uint32_t useCount = --header->useCount;

        //DSPEW("header->useCount = %" PRIu32 , useCount);

        if(useCount == 0)
            releaseBuffer(header);
        // else this buffer is being used in a filter in
        // another thread.
        buffers.pop();
    }

    if(h)
    {
        --h->useCount;
        if(h->useCount == 0)
            releaseBuffer(h);
    }
}
