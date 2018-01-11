#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "crts/debug.h"
#include "crts/Filter.hpp" // CRTSFilter user module interface
#include "FilterModule.hpp" // opaque co-class


FilterModule::FilterModule(void):
    readers(0), writers(0), readerIndexes(0),
    numReaders(0), numWriters(0)
{
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
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    size_t len;
};

// A struct with added memory to the bottom of it.
struct Buffer
{
    struct Header header;

    // This pointer should stay aligned so we can offset
    // to this pointer and set this pointer to any struct
    // or array that we want.
    uint8_t ptr[1]; // just a pointer that already has 1 byte
    // plus the memory below this struct.  The compiler guarantees that
    // ptr is memory aligned because it is 1 byte in a structure.
};

// Size of the buffer with added buf memory.
#define BUFFER_SIZE(x)  (sizeof(struct Buffer) + (x-1))

// To access the top of the buffer from the ptr pointer.
#define BUFFER_HEADER(ptr)\
    ((struct Header*) (((uint8_t*) ptr) - sizeof(struct Buffer) + 1))

#define BUFFER_PTR(top)\
    ((void*) (((uint8_t*) top) + sizeof(struct Buffer) - 1))


const uint32_t CRTSFilter::defaultBufferQueueLength = 3;


CRTSFilter::~CRTSFilter(void) { DSPEW(); }


CRTSFilter::CRTSFilter(void):
    bufferQueueLength(CRTSFilter::defaultBufferQueueLength)
{
    DSPEW();
}


void CRTSFilter::writePush(void *buffer, size_t bufferLen, uint32_t channelNum)
{
    DASSERT(filterModule->numReaders > channelNum,
            "!(filterModule->numReaders=%" PRIu32
            " > channelNum=%" PRIu32 ")",
            filterModule->numReaders, channelNum);

    // This filter writes to the connected reader filter at
    // the channel index channelNum.
    filterModule->readers[channelNum]->write(buffer, bufferLen,
            filterModule->readerIndexes[channelNum]);
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
//   2.  repackage the data using the same box (buffer) it can in
//
//
void *CRTSFilter::getBuffer(size_t bufferLen, bool canReuse)
{
    void *ret = malloc(BUFFER_SIZE(bufferLen));
    ASSERT(ret, "malloc() failed");
    ((struct Buffer*) ret)->header.len = bufferLen;
    return BUFFER_PTR(ret);
}


void CRTSFilter::releaseBuffer(void *buffer)
{

}


void CRTSFilter::setBufferQueueLength(uint32_t n)
{
    bufferQueueLength = n;
}
