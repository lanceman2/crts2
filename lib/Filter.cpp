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


CRTSFilter::CRTSFilter(void):
    bufferQueueLength(CRTSFilter::defaultBufferQueueLength)
{
    DSPEW();
}


void CRTSFilter::writePush(void *buffer, size_t bufferLen, uint32_t channelNum)
{

}


void *CRTSFilter::getBuffer(size_t bufferLen)
{
    return 0;
}

void CRTSFilter::releaseBuffer(void *buffer, ssize_t nWritten)
{

}

void CRTSFilter::setBufferQueueLength(uint32_t n)
{
    bufferQueueLength = n;
}
