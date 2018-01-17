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


Stdin::Stdin(int argc, const char **argv)
{
    DSPEW();
#ifdef DEBUG // TODO: remove this DEBUG SPEW
    DSPEW("  GOT ARGS");
    for(int i=0; i<argc; ++i)
        DSPEW("    ARG[%d]=\"%s\"", i, argv[i]);
    DSPEW();
#endif
}

ssize_t Stdin::write(void *buffer, size_t len, uint32_t channelNum)
{
    // This filter is a source so there no data passed to
    // whatever called this write().
    DASSERT(buffer == 0, "");

    if(feof(stdin)) return 0; // We are done.

 
    // Recycle the buffer and len argument variables.
    len = 1024;
    buffer = (uint8_t *) getBuffer(len);
    DASSERT(buffer, "");

    // This filter is a source, it reads stdin which is not a
    // part of this filter stream.
    size_t ret = fread(buffer, 1, len, stdin);

    if(ret != len)
        NOTICE("fread(,1,%zu,stdin) only read %zu bytes", len, ret);

    // Send this buffer to the next readers write call.
    writePush(buffer, len, channelNum);

    return len;
}


// Define the module loader stuff to make one of these class objects.
CRTSFILTER_MAKE_MODULE(Stdin)
