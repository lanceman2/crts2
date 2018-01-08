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
#include <string>
#include <atomic>
#include <uhd/usrp/multi_usrp.hpp>

#include "debug.h"

#include "get_opt.hpp"
#include "LoadModule.hpp"
// Read comments in ../include/crts/Filter.hpp.
#include "crts/Filter.hpp"
#include "pthread_wrappers.h" // some pthread_*() wrappers

class FilterModule;

// A singleton factory of FilterModule class objects.
//
// Keeps a list (map) of FilterModules.
class FilterModules : private std::map<uint32_t, FilterModule*>
{
    public:

        FilterModules(void);
        ~FilterModules(void);

        bool load(const char *name, int argc, const char **argv);

        bool connect(uint32_t from, uint32_t to);

    private:

        // Never decreases.
        uint32_t loadCount;
};

FilterModules::FilterModules(): loadCount(0)
{
    DSPEW();
}


FilterModules::~FilterModules()
{
    DSPEW();
}

// A class for keeping the CRTSFilter loaded modules with other data.
// Basically a stupid container struct, because we wanted to decrease the
// amount of data in the CRTSFilter (private or otherwise) which will tend
// to make the user interface change less.  We can add more stuff to this
// and the user interface will not change at all.  Even adding private
// data to the user interface in the class CRTSFilter will change the API
// (application programming interface) and more importantly ABI
// (application binary interface), so this cuts down on interface changes.
//
// And that's a big deal.
class FilterModule
{
    public:

        CRTSFilter *filter;

        void *(*destroyFilter)(CRTSFilter *);

        int loadIndex; // From FilterModules::loadCount

        inline void setReader(CRTSFilter *s)
        {
            filter->reader = s;
        }

        inline void setWriter(CRTSFilter *s)
        {
            filter->writer = s;
        }
};


// Return false on success.
bool FilterModules::load(const char *name, int argc, const char **argv)
{
    FilterModule *sm = new FilterModule;
    
    sm->filter = LoadModule<CRTSFilter>(name, "Filter",
            argc, argv, sm->destroyFilter);

    if(!sm->filter || !sm->destroyFilter)
        goto fail;

    this->insert(std::pair<uint32_t, FilterModule*>(++loadCount, sm));
    sm->loadIndex = loadCount;
    
    return false; // success

fail:

    delete sm;
    return true; // failure
}

// Return false on success.
bool FilterModules::connect(uint32_t from, uint32_t to)
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


    // connect these two filter in this direction:
    f->setReader(t->filter); // t is the reader
    t->setWriter(f->filter); // f is the writer

    return false; // success
}

static FilterModules filterModules;



// TODO: bool FilterModules::unload()





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
static const int exitSignals[] = { SIGINT, 0/*0 terminator*/ };


static pthread_t _mainThread = pthread_self();


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
        "  Usage: %s [OPTIONS]\n"
        "\n"
        "    Run the Cognitive Radio Test System (CRTS) transmitter/receiver program.\n"
        "\n"
          "\n", argv0);

    return 1; // return error status
}



static void signalExitProgramCatcher(int sig)
{
    DSPEW("Caught signal %d", sig);

    // Tell the RX and TX threads to finish.
}



int main(int argc, const char **argv)
{
    if(argc > 1)
        return usage(argv[0]);

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
            ASSERT(sigaction(exitSignals[i], &act, 0) == 0, "");
    }

    {
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


    {
        // Default list of modules.  0 terminated.
        // By default this runs like the UNIX program "cat"
        const char *modules[] =
        { 
            "stdin", "passThrough", "stdout",
            0
        };

        // Default module flow connectivity: connect 0 -> 1, 1 -> 2, 2 -> 3
        //
        // Connections are pairs of module array indexes that is
        // -1 terminated
        uint32_t connections[] =
        {
            0, 1, 1, 2, 3, 4, // a flow.  A single filter
            // 0 being a source and 1 being a sink
            (uint32_t) -1/*terminator*/
        };

        for(const char **mod = modules; mod; mod++)
            if(filterModules.load(*mod, argc-1, &argv[1]))
                return 1; // fail

        for(uint32_t *i = connections;
                *i != (uint32_t) -1 && *(i+1) != (uint32_t) -1 ;
                ++i)
            if(filterModules.connect(*i, *(i+1)))
                return 1;


        // TODO: parse command line to change modules list, module arguments and
        // module connectivity.

        // TODO: Add checking of module connectivity, so that they make sense.
    }


    // RANT:
    //
    // It'd be real nice if the UHD API would document what is thread-safe
    // and what is not for all the API.  We can only guess how to use this
    // stupid UHD API by looking at example codes.
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

    DSPEW("FINISHED");

    return 0;
}
