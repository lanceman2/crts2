#include <stdio.h>

#include "crts/RadioIO.hpp"
#include "crts.h" // for:  FILE *crtsOut

//#define DSPEW() /*empty macro*/
#define DSPEW() fprintf(stderr, "%s:%d:%s()\n", __FILE__, __LINE__, __func__)

class Stdout : public CRTSRadioIO
{
    public:

        Stdout(int argc, const char **argv);

        ssize_t write(const void *buffer, size_t bufferLen);
};

Stdout::Stdout(int argc, const char **argv) {DSPEW();}

ssize_t Stdout::write(const void *buffer, size_t bufferLen)
{
   return (ssize_t) fwrite(buffer, 1, bufferLen, crtsOut);
}


// Define the module loader stuff to make one of these class objects.
CRTSRadioIO_MAKE_MODULE(Stdout)
