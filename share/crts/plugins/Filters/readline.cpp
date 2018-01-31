#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "crts/debug.h"
#include "crts/Filter.hpp"
#include "crts/crts.h" // for:  FILE *crtsOut


class Readline : public CRTSFilter
{
    public:

        Readline(int argc, const char **argv);
        ~Readline(void);

        ssize_t write(void *buffer, size_t len, uint32_t channelNum);

    private:

        bool reading; // So can we exit do to command from read()
        const char *prompt;
        const char *sendPrefix;
        char *line; // last line read buffer
        size_t sendPrefixLen;
        ssize_t cleanExit(void);
        size_t promptLen;
};


static void usage(const char *arg)
{
    char name[64];

    fprintf(stderr, "module: %s: unknown option arg=\"%s\"\n",
            CRTSFILTER_NAME(name, 64), arg);

    fprintf(stderr,
"\n"
"\n"
"Usage: %s [ OPTIONS ]\n"
"\n"
"  OPTIONS are optional.\n"
"\n"
"  As an example you can run something like this:\n"
"\n"
"       crts_radio -f rx [ --uhd addr=192.168.10.3 --freq 932 ] -f stdout\n"
"\n"
"\n"
"  ---------------------------------------------------------------------------\n"
"                           OPTIONS\n"
"  ---------------------------------------------------------------------------\n"
"\n"
"\n"
"    --prompt PROMPT   \n"
"\n"
"\n"
"\n  --send-prefix PREFIX   \n"
"\n"
"\n"
"\n"
"\n",
    CRTSFILTER_NAME(name, 64));

    errno = 0;
    throw "usage help"; // This is how return an error from a C++ constructor
    // the module loader will catch this throw.
}


Readline::Readline(int argc, const char **argv):
    reading(true), prompt("> "),
    sendPrefix("received: "), line(0)
{
    int i = 0;

    while(i<argc)
    {
        // TODO: Keep argument option parsing simple??
        //
        // argv[0] is the first argument
        //
        if(!strcmp("--prompt", argv[i]) && i+1 < argc)
        {
            prompt = argv[++i];
            ++i;
            continue;
        }
        if(!strcmp("--send-prefix", argv[i]) && i+1 < argc)
        {
            sendPrefix = argv[++i];
            ++i;
            continue;
        }

        usage(argv[i]);
    }

    promptLen = strlen(prompt);
    
    // Because libuhd pollutes stdout we must use a different readline
    // prompt stream:
    rl_outstream = crtsOut;

    sendPrefixLen = strlen(sendPrefix);
}

Readline::~Readline(void)
{
    if(line)
    {
        free(line);
        line = 0;
    }

    // TODO: cleanup readline?

}


// From crts_radio.cpp
extern void crtsExit(void);


ssize_t Readline::cleanExit(void)
{
    crtsExit();
    // To stop the race so read don't go to another blocking
    // readline() call before the mainThread can stop this
    // thread.
    reading = false;
    return 0;
}


#define IS_WHITE(x)  ((x) < '!' || (x) > '~')
//#define IS_WHITE(x)  ((x) == '\n')

// TODO: readline makes extra copies of the input data, and then we have
// to copy it yet again to the input buffer.  All we really need it to do
// is write to the buffer pointed to by the pointer passed in the function
// argument, buffer.  Oh well.  It's small beans any way.  Inefficient,
// but small price for lots of functionally that readline provides.  The
// amount of data read by readline is usually small anyway.
//
// We may consider using isatty() to choose between modules readline.cpp
// and fd0.cpp.
//
//
// This call will block until we get input.
//
//
// This runs in another thread from Readline::read()
// We write to crtsOut
ssize_t Readline::write(void *buffer, size_t bufferLen, uint32_t channelNum)
{
    // This module is a source.
    DASSERT(!buffer, "");

    // Repurpose the passed in variables buffer and bufferLen
    bufferLen = 1024;
    buffer = getBuffer(bufferLen);

    while(reading)
    {
        // get a line
        line = readline(prompt);
        if(!line) return cleanExit();

        //fprintf(stderr, "%s:%d: GOT: \"%s\"  prompt=\"%s\"\n", __BASE_FILE__, __LINE__, line, prompt);

        // Strip off trailing white chars:
        size_t len = strlen(line);
        while(len && IS_WHITE(line[len -1]))
            line[--len] = '\0';

        //fprintf(crtsOut, "\n\nbuffer=\"%s\"\n\n", line);

        if(len < 1)
        {
            free(line);
            line = 0;
            continue;
        }

        // TODO: add tab help, history saving, and other readline user
        // interface stuff.

        if(!strcmp(line, "exit") || !strcmp(line, "quit"))
            return cleanExit();

        // Return char counter:
        ssize_t nRet = 0;


        // TODO: Skip the prefix if it's too long ??
        // Better than dumb segfault, for now...
        if(sendPrefixLen && sendPrefixLen < bufferLen)
        {
            // Put the prefix in the buffer first:
            memcpy(buffer, sendPrefix, sendPrefixLen);
            // Update remaining buffer length and advance the buffer pointer
            bufferLen -= sendPrefixLen;
            buffer = (void *) (((char *) buffer) + sendPrefixLen);
            nRet += sendPrefixLen;
        }

        // We put the '\0' terminator in the buffer too:
        //
        if(len < bufferLen)
        {
            // Very likely case for a large buffer.  We eat it all at
            // once, null ('\0') terminator and all.
            memcpy(buffer, line, len + 1);
            // We don't need to send the '\0'
            nRet += len;
        }
        else
        {
            // In this case we just ignore the extra data.
            memcpy(buffer, line, bufferLen -1);
            // We add a '\0' terminator as the last char
            // in the buffer.
            ((char *)buffer)[bufferLen - 1] = '\0';
            // We don't need to send the '\0'
            nRet += bufferLen;
        }

        // We do not add the sendPrefix to the history.  Note: buffer was
        // '\0' terminated just above, so the history string is cool.
        add_history((char *) buffer);

        free(line); // reset readline().
        line = 0;

        writePush(buffer, nRet, CRTSFilter::ALL_CHANNELS);
    }

    // TODO: We set reading to false and so we just return 0 as a EOF signal?
    return 0;
}


// Define the module loader stuff to make one of these class objects.
CRTSFILTER_MAKE_MODULE(Readline)
