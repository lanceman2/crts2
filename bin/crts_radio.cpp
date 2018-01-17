#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <inttypes.h>
#include <map>
#include <list>
#include <vector>
#include <string>
#include <atomic>
#include <uhd/usrp/multi_usrp.hpp>

#include "crts/debug.h"

#include "get_opt.hpp"
#include "LoadModule.hpp"
// Read comments in ../include/crts/Filter.hpp.
#include "crts/Filter.hpp"
#include "FilterModule.hpp"
#include "Stream.hpp"
#include "pthread_wrappers.h" // some pthread_*() wrappers



std::list<Stream*> Stream::streams;

// Compiles the list of sources after connections are finished being
// added.
void Stream::getSources(void)
{
    sources.clear();
    for(auto &val : map)
        if(!val.second->writers)
            // It has no writers so val->second is a FilterModule that is
            // a source.
            sources.push_back(val.second);

    // We need there to be at least one source FilterModule
    DASSERT(sources.size() > 0, "");
}

Stream *Stream::createStream(void)
{
    DSPEW();
    Stream *s = new Stream;
    streams.push_back(s);
    DSPEW("now there are %d Streams", streams.size());
    return s;
}


Stream::Stream(void): map(*this),
    isRunning(true), checkedConnections(false), loadCount(0)
{
    DSPEW();
}


Stream::~Stream(void)
{
    DSPEW();
    
    sources.clear();

    streams.remove(this);

    DSPEW("now there are %d Streams", streams.size());
}

void Stream::destroyStreams(void)
{
    DSPEW("streams.size()=%d", streams.size());

    auto it = streams.begin();
    for(;it != streams.end(); it = streams.begin())
        delete (*it);

    DSPEW("streams.size()=%d", streams.size());
}
    



// Return false on success.
bool Stream::load(const char *name, int argc, const char **argv)
{
    FilterModule *m = 0;

    void *(*destroyFilter)(CRTSFilter *);

    CRTSFilter *crtsFilter = LoadModule<CRTSFilter>(name, "Filters",
            argc, argv, destroyFilter);

    if(!crtsFilter || !destroyFilter)
        return true; // fail

    m = new FilterModule(this, crtsFilter, destroyFilter, loadCount);

    this->insert(std::pair<uint32_t, FilterModule*>(loadCount, m));

    ++loadCount;

    return false; // success
}

// Return false on success.
bool Stream::connect(uint32_t from, uint32_t to)
{
    if(from == to)
    {
        ERROR("The filter numbered %" PRIu32
                " cannot be connected to its self");
        return true; // failure
    }

    std::map<uint32_t,FilterModule*>::iterator it;

    it = this->find(from);
    if(it == this->end())
    {
        ERROR("There is no filter numbered %" PRIu32, from);
        return true; // failure
    }
    FilterModule *f = it->second;

    it = this->find(to);
    if(it == this->end())
    {
        ERROR("There is no filter numbered %" PRIu32, to);
        return true; // failure
    }
    FilterModule *t = it->second;


    ////////////////////////////////////////////////////////////
    // Connect these two filters in this direction
    // like a doubly linked list from one filter to another.
    ////////////////////////////////////////////////////////////

    // TODO: Currently using arrays to construct a doubly linked list
    // which will allow very fast access, but slow editing.

    // In the "f" filter we need to writePush() to readers telling the "t"
    // reader filter it's channel index is the next one,
    // t->filter->numWriters.  Think, we write to readers.

    f->readers = (CRTSFilter**) realloc(f->readers,
            sizeof(CRTSFilter*)*(f->numReaders+1));
    ASSERT(f->readers, "realloc() failed");
    f->readers[f->numReaders] = t->filter; // t is the reader from f

    f->readerIndexes = (uint32_t *) realloc(f->readerIndexes,
            sizeof(uint32_t)*(f->numReaders+1));
    ASSERT(f->readerIndexes, "realloc() failed");
    // We are the last channel in the "t" writer list
    f->readerIndexes[f->numReaders] = t->numWriters;

    // The "t" filter needs to point back to the "f" filter so that we can
    // see and edit this connection from the "f" or "t" side, like it's a
    // doubly linked list.  If not for editing this "connection list", we
    // would not need this t->writers[].
    t->writers = (CRTSFilter**) realloc(t->writers,
            sizeof(CRTSFilter*)*(t->numWriters+1));
    ASSERT(t->writers, "realloc() failed");
    t->writers[t->numWriters] = f->filter; // f is the writer to t


    ++f->numReaders;
    ++t->numWriters;


    // Set this flag so we know there was at least one connection.
    checkedConnections = true;


    DSPEW("Connected % " PRIu32 " to %" PRIu32, from, to);

    return false; // success
}




// TODO: bool Stream::unload()





// Shared in threadShared.hpp
//
// For thread safety of lib FFTW-3 ofdmflexframe create and destroy
// functions.  See additional comments in threadShared.hpp
pthread_mutex_t fftw3_mutex = PTHREAD_MUTEX_INITIALIZER;


// We can add signals to this list that is 0 terminated.  Signals that we
// use to gracefully exit with, that is catch the signal and then set the
// atomic flags and wait for the threads to finish the last loop, if they
// have not already.
//
//   SIGINT is from Ctrl-C in a terminal.
//
static const int exitSignals[] =
{
    SIGINT,
    //
    // TODO: add more clean exit signal numbers here.
    //
    0/*0 terminator*/
};


static pthread_t _mainThread = pthread_self();



// TODO: Extend this to work for the case when there is more than one
// stream.
//
// This is a module user interface that may be called from another thread.
//
// Try to gracefully exit.
void crtsExit(void)
{
    errno = 0;
    // We signal using just the first exit signal in the list.
    INFO("Sending signal %d to main thread", exitSignals[0]);
    errno = pthread_kill(_mainThread, exitSignals[0]);
    // All we could do is try and report.
    WARN("Signal %d sent to main thread", exitSignals[0]);

    // TODO: cleanup all thread and processes

    for(auto stream : Stream::streams)
        // Let it finish the last loop:
        stream->isRunning = false;
}



static void badSigCatcher(int sig)
{
    ASSERT(0, "caught signal %d", sig);
}


static int usage(const char *argv0, const char *uopt=0)
{
    // Keep this function consistent with the argument parsing:

    if(uopt)
        printf("\n Unknown option: %s\n\n\n", uopt);

    printf(
        "\n"
        "  Usage: %s OPTIONS\n",
        argv0);

    printf(
        "\n"
        "    Run the Cognitive Radio Test System (CRTS) "
        "transmitter/receiver program.\n"
        " Some -f and -c argument options are required\n"
        "\n"
        "\n"
        "                   OPTIONS\n"
        "\n"
        "\n"
        "   -c | --connect LIST              how to connect the loaded filters"
                                            "that are in the current stream\n"
        "                                   Example:\n"
        "\n"
        "                                       -c \"0 1 1 2\"\n"
        "\n"
        "                                    connect from filter 0 to filter 1"
                                            " and from filter 1 to filter 2.\n"
        "                                    This option must follow all the"
                                            " corresponding FILTER options\n"
        "                                    Arguments follow a connection LIST"
                                            " will be in a new Stream.\n"
        "\n"
        "\n"
        "   -f | --filter FILTER [OPTS ...]   load filter module FILTER\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n");

    return 1; // return error status
}



static void signalExitProgramCatcher(int sig)
{
    INFO("Caught signal %d waiting to cleanly exit", sig);

    // Deal with multi-stream
    // Let them finish the last/current loop

    for(auto stream : Stream::streams)
        stream->isRunning = false;
}

int setDefaultStreamConnections(Stream* &stream)
{
    // default connections: 0 1   1 2   2 3   3 4   4 5  ...
    // default connections: 0 -> 1   1 -> 2   2 -> 3   3 -> 4   4 -> 5  ...

    DASSERT(stream->checkedConnections == false, "");

    uint32_t from = 0;

    for(auto toFilterModule : *stream)
        if(toFilterModule.second->loadIndex > 0)
            if(stream->connect(from++, toFilterModule.second->loadIndex))
                return 1; // fail

    // We set this flag here in case there was just one filter and
    // not really any connections.
    stream->checkedConnections = true;

    return 0; // success
}



/////////////////////////////////////////////////////////////////////
// Parsing command line arguments
/////////////////////////////////////////////////////////////////////
//
static int parseArgs(int argc, const char **argv)
{
    // Current stream as a pointer.  There are none yet.
    Stream *stream = 0;

    class ConnectionPair // just two integers in a pair
    {
        public:
            inline ConnectionPair(uint32_t from_, uint32_t to_):
                    from(from_), to(to_)
            { 
                DSPEW("%" PRIu32 ",%" PRIu32, from, to);
            };
            // Cool, looks like the copy constructor is automatic.

#if 1 // debugging
            // Checking that memory is cleaned up.
            inline ~ConnectionPair(void)
            {
                DSPEW("%" PRIu32 ",%" PRIu32, from, to);
            };
#endif
            uint32_t from, to; // filter index pair starting with 0
    };


    int i = 1;
    if(i >= argc)
        return usage(argv[0]);

    while(i < argc)
    {
        // These may get set if we have the necessary options:
        std::string str;
        int Argc = 0;
        const char **Argv = 0;
        double rx_freq = 0; // TODO: remove this.

        if(get_opt(str, Argc, Argv, "-f", "--filter", argc, argv, i))
        {
            if(!stream)
                // We add filters a new "current" stream until we add
                // connections.
                stream = Stream::createStream();

            DSPEW("got optional arg filter:  %s", str.c_str());

            // Add the filter to the current stream in the command-line.
            stream->load(str.c_str(), Argc, Argv);
            continue;
        }

        if((!strcmp("-c", argv[i]) || !strcmp("--connect", argv[i])))
        {
            ++i;

            // We read the "from" and "to" channel indexes in pairs
            while(i < argc + 2 &&
                    argv[i][0] > '0' && argv[i][0] < '9' &&
                    argv[i+1][0] > '0' && argv[i+1][0] < '9')
            {
                // the follow args are like:  0 1 1 2
                // We get them in pairs like: 0 1 and 1 2
                uint32_t from, to;
                errno = 0;
                from = strtoul(argv[i], 0, 10);
                if(errno || from == ULONG_MAX)
                    return usage(argv[0], argv[i]);
                ++i;
                to = strtoul(argv[i], 0, 10);
                if(errno || to == ULONG_MAX)
                    return usage(argv[0], argv[i]);
                // Connect filters in the current stream.
                if(stream->connect(from, to))
                    return usage(argv[0], argv[i]);
                ++i;
            }

            if(!stream->checkedConnections)
                if(setDefaultStreamConnections(stream))
                    return 1; // failure

            // Ready to make a another stream if more filters are added.
            stream = 0;
            // The global Stream::streams will keep the list of streams
            // for us.
        }


        if(get_opt_double(rx_freq, "-f", "--rx-freq", argc, argv, i))
            // TODO: remove this if() block
            continue;
        
        if(!strcmp("-h", argv[i]) || !strcmp("--help", argv[i]))
            return usage(argv[0]);

        return usage(argv[0], argv[i]);

        //++i;
    }

    if(stream && !stream->checkedConnections)
        setDefaultStreamConnections(stream);


    // TODO: parse command line to change modules list, module arguments and
    // module connectivity.

    // TODO: Add checking of module connectivity, so that they make sense.

    return 0; // success
}



int main(int argc, const char **argv)
{
    // This will hang the process or thread if we catch the following
    // signals, so we can debug it and see what was wrong if we're
    // lucky.
    ASSERT(signal(SIGSEGV, badSigCatcher) == 0, "");
    ASSERT(signal(SIGABRT, badSigCatcher) == 0, "");
    ASSERT(signal(SIGFPE,  badSigCatcher) == 0, "");

    {
        // Setup the exit signal catcher.
        struct sigaction act;
        memset(&act, 0, sizeof(act));
        act.sa_handler = signalExitProgramCatcher;
        act.sa_flags = SA_RESETHAND;
        errno = 0;
        for(int i=0; exitSignals[i]; ++i)
        {
            ASSERT(sigaction(exitSignals[i], &act, 0) == 0, "");
            DSPEW("set clean exit catcher for signal %d", exitSignals[i]);
        }
    }

#if 0 // TODO: this code may be kruft, but could be used later.
    {
        // TODO: this code may be kruft, but could be used later.

        // We must not let the threads created by the UHD API catch the
        // exit signals, so they will inherit the blocking of the exit
        // signals after we set them here in the main thread.
        sigset_t sigSet;
        sigemptyset(&sigSet);
        for(int i=0; exitSignals[i]; ++i)
            sigaddset(&sigSet, exitSignals[i]);
        errno = pthread_sigmask(SIG_BLOCK, &sigSet, NULL);
        ASSERT(errno == 0, "pthread_sigmask() failed");
    }
#endif

    // TODO: For now we get one stream for free without --stream on the
    // command-line.  Each --stream arg on the command-line will make a
    // new Stream for all following args until the next --stream which
    // makes another.  It will have to be dynamically allocated because
    // we will not know how many "Streams" there will be until we parse
    // the command line.

    if(parseArgs(argc, argv))
        return 1; // failure


    ///////////////////////////////////////////////////////////////////
    // TODO: Check that there are connections in the stream if not
    // maybe make the "default" (0 1 1 2 2 3 ...) connections.
    ///////////////////////////////////////////////////////////////////


    for(auto stream : Stream::streams)
        stream->getSources();

    bool isRunning = true;

    while(isRunning)
    {
#ifdef DEBUG
        uint32_t numStreamsRunning = 0;
#endif

        isRunning = false;
        for(auto stream : Stream::streams)
        {
            if(stream->isRunning)
            {
                for(auto source : stream->sources)
                {
                    // Run it:
                    source->filter->write(0,0,0);

                    // Something ran so keep it running
                    isRunning = true;
                }
            }
#ifdef DEBUG
            ++numStreamsRunning;
#endif

        }
#ifdef DEBUG
        if(numStreamsRunning != Stream::streams.size())
            SPEW("%" PRIu32 " of %" PRIu32 " streams are running");
#endif
    }


    // RANT:
    //
    // It'd be real nice if the UHD API would document what is thread-safe
    // and what is not for all the API.  We can only guess how to use this
    // stupid UHD API by looking at example codes.  From the program
    // crashes I've seen there are clearly some things that are not thread
    // safe, or just bad code in libuhd.
    //
    // The structure of the UHD API implies that you should be able to use
    // a single uhd::usrp::multi_usrp::sptr to do both transmission and
    // receiving but none of the example do that, the examples imply that
    // you must make two uhd::usrp::multi_usrp::sptr objects one for
    // (TX) transmission and one for (RX) receiving.

    // register UHD message handler
    // Let it use stdout, or stderr by default???
    //uhd::msg::register_handler(&uhd_msg_handler);

    // We do not know where UHD is thread safe, so, for now, we do this
    // before we make threads.  The UHD examples do it this way too.
    // We set up the usrp (RX and TX) objects in the main thread here:

    // Testing segfault catcher by setting bad memory address
    // to an int value.
    //void *tret
    //*(int *) (((uintptr_t) tret) + 0xfffffffffffffff8) = 1;




    // UHD BUG WORKAROUND:
    //
    // We must make the two multi_usrp objects before we configure them by
    // setting frequency, rate (bandWidth), and gain; otherwise the
    // process exits with status 0.  And it looks like you can use the
    // same object for both receiving (RX) and transmitting (TX).
    // The UHD API seems to be a piece of shit in general.  Here we
    // are keeping a list of stupid shit it does, and a good API will
    // never do:
    //
    //    - calls exit; instead of throwing an exception
    //
    //    - spawns threads and does not tell you it does in the
    //      documentation
    //
    //    - spews to stdout (we made a work-around for this)
    //
    //    - catches signals
    //
    //
    // It may be libBOOST doing this shit...  so another thing
    // to add to the bad things list:
    //
    //   - links with BOOST
    //
    // We sometimes get
    // Floating point exception
    // and the program exits


    Stream::destroyStreams();

    DSPEW("FINISHED");

    return 0;
}
