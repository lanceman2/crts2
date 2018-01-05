#include <stdio.h>

#include "crts/Stream.hpp"
#include "debug.h"

struct Buffer
{
    void *ptr; // pointer to allocated memory
    pthread_cond_t cond;
    pthread_mutex_t mutex;
};


const uint32_t CRTSStream::defaultBufferQueueLength = 3;


CRTSStream::CRTSStream(void): reader(0), writer(0),
        bufferQueueLength(CRTSStream::defaultBufferQueueLength)
{
    DSPEW();
}

CRTSStream::~CRTSStream(void) { DSPEW(); }

void CRTSStream::setThreaded(void)
{

}

void CRTSStream::writePush(void *buffer, size_t bufferLen)
{
    if(!reader) return; // this is a stream sink

    reader->write(buffer, bufferLen);
}

void *CRTSStream::getBuffer(size_t bufferLen)
{
    return 0;
}

void CRTSStream::releaseBuffer(void *buffer)
{

}

void CRTSStream::setBufferQueueLength(uint32_t n)
{
    bufferQueueLength = n;
}
