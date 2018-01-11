#include <stdio.h>

#include "crts/debug.h"
#include "crts/Filter.hpp"


class Stdin : public CRTSFilter
{
    public:

        Stdin(int argc, const char **argv);

        ssize_t write(void *buffer, size_t bufferLen,
                uint32_t channelNum);
};


Stdin::Stdin(int argc, const char **argv) {DSPEW();}

ssize_t Stdin::write(void *buffer_in, size_t len_in, uint32_t channelNum)
{
    if(feof(stdin)) return 0; // We are done.

    // This filter is a source so there no data passed to
    // whatever called this write().
    DASSERT(buffer_in == 0, "");

    const size_t len = 1024;
    uint8_t *buffer = (uint8_t *) getBuffer(len);

    // This filter is a source, it reads stdin which is not a
    // part of this filter stream.
    size_t ret = fread(buffer, 1, len, stdin);

    if(ret != len)
        NOTICE("fread(,1,%zu,stdin) only read %zu bytes", len, ret);


    // Send this buffer to the next readers write call.
    writePush(buffer, len);

    // If this is a thread than let others use this buffer.
    releaseBuffer(buffer);

    return ret;
}


// Define the module loader stuff to make one of these class objects.
CRTSFILTER_MAKE_MODULE(Stdin)
