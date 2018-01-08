#include <stdio.h>

#include "crts/Filter.hpp"
#include "crts.h" // for:  FILE *crtsOut

//#define DSPEW() /*empty macro*/
#define DSPEW() fprintf(stderr, "%s:%d:%s()\n", __FILE__, __LINE__, __func__)

class Stdout : public CRTSFilter
{
    public:

        Stdout(int argc, const char **argv);

        ssize_t write(void *buffer, size_t bufferLen);
};

Stdout::Stdout(int argc, const char **argv) {DSPEW();}

ssize_t Stdout::write(void *buffer, size_t bufferLen)
{
    ssize_t ret = fwrite(buffer, 1, bufferLen, crtsOut);

    releaseBuffer(buffer);

    return ret;
}


// Define the module loader stuff to make one of these class objects.
CRTSFILTER_MAKE_MODULE(Stdout)
