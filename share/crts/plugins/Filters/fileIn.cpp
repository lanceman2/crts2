#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string>

#include "crts/debug.h"
#include "crts/Filter.hpp"
#include "crts/crts.h" // for:  FILE *crtsOut


class FileIn : public CRTSFilter
{
    public:

        FileIn(int argc, const char **argv);
        ~FileIn(void);

        ssize_t write(void *buffer, size_t bufferLen,
                uint32_t channelNum);
    private:

        FILE *file;
 };


// This is called if the user ran something like: 
//
//    crts_radio -f file [ --help ]
//
//
static void usage(void)
{
    char name[64];
    fprintf(stderr,
"\n"
"\n"
"Usage: %s [ OPTIONS ]\n"
"\n"
"\n"
"  ---------------------------------------------------------------------------\n"
"                           OPTIONS\n"
"  ---------------------------------------------------------------------------\n"
"\n"
"\n"
"   --file FILENAME   Read file FILENAME.  By default reads stdin.\n"
"\n"
"\n",
        CRTSFILTER_NAME(name, 64));

    errno = 0;
    throw "usage help"; // This is how return an error from a C++ constructor
    // the module loader will catch this throw.
}



FileIn::FileIn(int argc, const char **argv)
{
    int i;
#ifdef DEBUG
    DSPEW();
    if(argc>0)
        DSPEW("  GOT ARGS");
    for(i=0; i<argc; ++i)
        DSPEW("    ARG[%d]=\"%s\"", i, argv[i]);
#endif

    const char *filename = 0;

    for(i=0; i<argc; ++i)
    {
        if(!strcmp(argv[i], "--file") && i<argc+1)
        {
            filename = argv[++i];
            continue;
        }
        else
            usage();
    }

    if(filename)
    {
        errno = 0;
        file = fopen(filename, "r");
        if(!file)
        {
            ERROR("fopen(\"%s\", \"r\") failed", filename);
            throw "failed to open file";
        }
        INFO("opened file: %s", filename);
    }
    else
        file = stdin;
}


FileIn::~FileIn(void)
{
    DSPEW();

    // TODO: delete the usrp.  libuhd is a piece of shit so you can't.

    // TODO: What does this return:
    if(file != stdin)
        fclose(file);

    DSPEW();
}


ssize_t FileIn::write(void *buffer, size_t len, uint32_t channelNum)
{
    // This filter is a source so there no data passed to
    // whatever called this write().
    //
    // Source filters are different than non-source filters in that they
    // run a loop like this until they are stopped by the isRunning flag
    // or an external end condition like, in this case, end of file.
    //
    DASSERT(buffer == 0, "");

    while(stream->isRunning)
    {
        if(feof(file)) 
        {
            // end of file
            stream->isRunning = false;
            NOTICE("read end of file");
            return 0; // We are done.
        }
 
        // Recycle the buffer and len argument variables.
        len = 1024;
        // Get a buffer from the buffer pool.
        buffer = (uint8_t *) getBuffer(len);

        // This filter is a source, it reads file which is not a
        // part of this filter stream.
        size_t ret = fread(buffer, 1, len, file);

        // Since fread() can block we check if another thread unset
        // this flag:
        if(!stream->isRunning) break;

        if(ret != len)
            NOTICE("fread(,1,%zu,file) only read %zu bytes", len, ret);

        if(ret > 0)
            // Send this buffer to the next readers write call.
            writePush(buffer, ret, ALL_CHANNELS);
    }

    return 1;
}


// Define the module loader stuff to make one of these class objects.
CRTSFILTER_MAKE_MODULE(FileIn)
