#include <stdio.h>

#include "crts/Filter.hpp"
#include "debug.h"

struct Buffer
{
    void *ptr; // pointer to allocated memory
    pthread_cond_t cond;
    pthread_mutex_t mutex;
};


const uint32_t CRTSFilter::defaultBufferQueueLength = 3;


CRTSFilter::~CRTSFilter(void) { DSPEW(); }


CRTSFilter::CRTSFilter(void): reader(0), writer(0),
        bufferQueueLength(CRTSFilter::defaultBufferQueueLength)
{
    DSPEW();
}


void CRTSFilter::setThreaded(void)
{

}

void CRTSFilter::writePush(void *buffer, size_t bufferLen)
{
    if(!reader) return; // this is a stream sink

    reader->write(buffer, bufferLen);
}

void *CRTSFilter::getBuffer(size_t bufferLen)
{
    return 0;
}

void CRTSFilter::releaseBuffer(void *buffer)
{

}

void CRTSFilter::setBufferQueueLength(uint32_t n)
{
    bufferQueueLength = n;
}
