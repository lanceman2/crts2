#include <stdio.h>

#include "crts/Stream.hpp"
#include "crts.h" // for:  FILE *crtsOut

//#define DSPEW() /*empty macro*/
#define DSPEW() fprintf(stderr, "%s:%d:%s()\n", __FILE__, __LINE__, __func__)

class Stdout : public CRTSStream
{
    public:

        Stdout(int argc, const char **argv);

        ssize_t write(void *buffer, size_t bufferLen);
};

Stdout::Stdout(int argc, const char **argv) {DSPEW();}

ssize_t Stdout::write(void *buffer, size_t bufferLen)
{
   return (ssize_t) fwrite(buffer, 1, bufferLen, crtsOut);
}


// Define the module loader stuff to make one of these class objects.
CRTSSTREAM_MAKE_MODULE(Stdout)
