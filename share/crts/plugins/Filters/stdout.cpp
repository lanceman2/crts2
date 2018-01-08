#include <stdio.h>

#include "crts/Filter.hpp"
#include "crts.h" // for:  FILE *crtsOut

//#define DSPEW() /*empty macro*/
#define DSPEW() fprintf(stderr, "%s:%d:%s()\n", __FILE__, __LINE__, __func__)

class Stdout : public CRTSFilter
{
    public:

        Stdout(int argc, const char **argv);

        ssize_t write(void *buffer, size_t bufferLen,
                uint32_t channelNum);
};

Stdout::Stdout(int argc, const char **argv) {DSPEW();}

ssize_t Stdout::write(void *buffer, size_t len, uint32_t channelNum)
{
    ssize_t ret = fwrite(buffer, 1, len, crtsOut);

    // This filter is a sink, the end of the line, so we do not need to
    // writePush().

    releaseBuffer(buffer, ret);

    return ret;
}


// Define the module loader stuff to make one of these class objects.
CRTSFILTER_MAKE_MODULE(Stdout)
