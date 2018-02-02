#include <stdio.h>
#include <string.h>
#include <string>

#include "crts/debug.h"
#include "crts/Filter.hpp"
#include "crts/crts.h" // for:  FILE *crtsOut


class File : public CRTSFilter
{
    public:

        File(int argc, const char **argv);
        ~File(void);

        ssize_t write(void *buffer, size_t bufferLen, uint32_t channelNum);

    private:

        FILE *file;
        const char *filename;

};


// This is called if the user ran something like: 
//
//    crts_radio -f file [ --help ]
//
//
static void usage(void)
{
    char name[64];
    fprintf(crtsOut, "Usage: %s [ OUT_FILENAME ]\n"
            "\n"
            "  The option OUT_FILENAME is optional.\n"
            "\n"
            "\n"
            , CRTSFILTER_NAME(name, 64));

    throw ""; // This is how return an error from a C++ constructor
    // the module loader with catch this throw.
}


File::File(int argc, const char **argv): file(0), filename("crts_file.txt")
{
    DSPEW();
#ifdef DEBUG // TODO: remove this DEBUG SPEW
    DSPEW("  GOT ARGS");
    for(int i=0; i<argc; ++i)
        DSPEW("    ARG[%d]=\"%s\"", i, argv[i]);
    DSPEW();
#endif

    if(argc > 1 || (argc == 1 && argv[0][0] == '-'))
        usage();
    else if(argc == 1)
        filename = argv[0];

    file = fopen(filename, "a");
    if(!file)
    {
        std::string str("fopen(\"");
        str += filename;
        str += "\") failed";
        // This is how return an error from a C++ constructor
        // the module loader with catch this throw.
        throw str;
    }

    DSPEW();
}


File::~File(void)
{
    if(file)
    {
        fclose(file);
        file = 0;
    }

    DSPEW();
}


ssize_t File::write(void *buffer, size_t len, uint32_t channelNum)
{
    DASSERT(buffer, "");
    DASSERT(len, "");

    // This filter is a sink, the end of the line, so we do not call
    // writePush().  We write no matter what channel it is.
    errno = 0;

    size_t ret = fwrite(buffer, 1, len, file);

    if(ret != len && errno == EINTR)
    {
        // One more try, because we where interrupted
        errno = 0;
        ret += fwrite(buffer, 1, len - ret, file);
    }

    if(ret != len)
        NOTICE("fwrite(,1,%zu,) only wrote %zu bytes", len, ret);

    fflush(file);

    return ret;
}


// Define the module loader stuff to make one of these class objects.
CRTSFILTER_MAKE_MODULE(File)
