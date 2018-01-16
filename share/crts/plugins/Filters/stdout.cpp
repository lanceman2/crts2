#include <stdio.h>

#include "crts/debug.h"
#include "crts/Filter.hpp"
#include "crts.h" // for:  FILE *crtsOut


class Stdout : public CRTSFilter
{
    public:

        Stdout(int argc, const char **argv);

        ssize_t write(void *buffer, size_t bufferLen,
                uint32_t channelNum);
};


Stdout::Stdout(int argc, const char **argv)
{
    DSPEW();
}

ssize_t Stdout::write(void *buffer, size_t len, uint32_t channelNum)
{
    DASSERT(channelNum == 0, "");
    DASSERT(buffer, "");
    DASSERT(len, "");

    // This filter is a sink, the end of the line, so we do not call
    // writePush().  crtsOut is like stdout if not for libuhd screwing up
    // stdout.   It writes crtsOut which is not part of the filter stream.

    errno = 0;

    size_t ret = fwrite(buffer, 1, len, crtsOut);

    if(ret != len && errno == EINTR)
    {
        // One more try because
        errno = 0;
        ret = fwrite(buffer, 1, len, crtsOut);
    }

    if(ret != len)
        NOTICE("fwrite(,1,%zu,crtsOut) only read %zu bytes", len, ret);

    // End of the filter stream line, so recycle the buffer.
    releaseBuffer(buffer);

    return ret;
}


// Define the module loader stuff to make one of these class objects.
CRTSFILTER_MAKE_MODULE(Stdout)
