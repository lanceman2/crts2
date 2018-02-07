#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <inttypes.h>
#include <time.h>
#include <map>
#include <list>
#include <vector>
#include <string>
#include <stack>
#include <atomic>
//#include <uhd/usrp/multi_usrp.hpp>

#include "crts/debug.h"

#include "get_opt.hpp"
#include "LoadModule.hpp"
// Read comments in ../include/crts/Filter.hpp.
#include "pthread_wrappers.h" // some pthread_*() wrappers
#include "crts/Filter.hpp"
#include "FilterModule.hpp"
#include "Thread.hpp"
#include "Stream.hpp"



std::list<Stream*> Stream::streams;

// static Stream mutex and condition variables for
// Stream::wait()
//
pthread_mutex_t Stream::mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t Stream::cond = PTHREAD_COND_INITIALIZER;


// Waits until a stream and all it's threads is cleaned up.  Returns the
// number of remaining running streams.
//
// Only the main thread will delete streams and only when the atomic bool
// isRunning is set to false. So we do not need a mutex to check this flag
// because it is atomic and it is only set to false once.  We call when
// all streams use the isRunning flags, a graceful exit.
//
//
// We must has the streams mutex lock before calling this.
//
size_t Stream::wait(void)
{
    // We assume that the list of streams is a small list, like
    // the number of cores on the CPU.

    // This is the main thread.
    DASSERT(pthread_equal(Thread::mainThread, pthread_self()), "");

    // Only this thread can change the streams.size().


    // No thread may signal this thread until they
    // get this mutex lock.

    if(streams.size())
    {
        for(auto stream: streams)
        {
            if(!stream->isRunning)
            {
                // It's okay that we edit this streams list, because will
                // will return without going to the next element.
                //
                // We cannot delete the stream while holding the lock.
                // We don't need the lock to delete the stream.
                //
                MUTEX_UNLOCK(&mutex);
                delete stream;
                // Only this thread can change the streams.size() so it's
                // okay to call size().
                return streams.size();
            }
        }
    }
    else
    {
        MUTEX_UNLOCK(&mutex);
        return 0; // We're done.  There are no streams left.
    }

    // All streams have isRunning set or are about to signal us after they
    // get the mutex lock.  The threads that are setting stream->isRunning
    // to false now, will signal us in their last block of code in their
    // pthread callback where they block getting the mutex before
    // signaling.  I.E. this should work in all cases so long as
    // this is the main thread.

    DSPEW("waiting for %" PRIu32 " stream(s) to finish", streams.size());

    // Here we loose the mutex lock and wait
    // This COND_WAIT() macro wrapper recall the pthread_cond_wait()
    // if a signal interrupts the call.
    ASSERT((errno = pthread_cond_wait(&cond, &mutex)) == 0, "");

    DSPEW("Got cond signal after waiting for %" PRIu32 " stream(s)",
            streams.size());

    // Now we have the mutex lock again.

    // The number of streams should not have changed while we where
    // waiting.
    DASSERT(streams.size(), "streams.size()=%zu", streams.size());

    // We should be able to find a stream with isRunning == false.
    for(auto stream: streams)
    {
        if(!stream->isRunning)
        {
            // We cannot delete the stream while holding the lock.
            // We don't need the lock to delete the stream.
            //
            MUTEX_UNLOCK(&mutex);
            delete stream;
            // Only this thread can change the streams.size() so it's okay
            // to call size().
            return streams.size();
        }
    }

    // We should not get here.  It would mean that other threads are
    // deleting streams, and we assumed that that can't happen.  Only the
    // main thread can delete streams.

    DASSERT(0, "There are no streams with isRunning false,"
            " streams.size()=%zu", streams.size());

    MUTEX_UNLOCK(&mutex);

    // We should not get here.

    return streams.size();
}


// Compiles the list of sources after connections are finished being
// added.
void Stream::getSources(void)
{
    // This is the main thread.
    DASSERT(pthread_equal(Thread::mainThread, pthread_self()), "");

    sources.clear();
    for(auto &val : map)
        if(val.second->isSource())
            // It has no writers so val->second is a FilterModule that is
            // a source.
            sources.push_back(val.second);

    // We need there to be at least one source FilterModule
    DASSERT(sources.size() > 0, "");
}


Stream::Stream(void): map(*this),
    isRunning(true), haveConnections(false), loadCount(0)
{ 
    // This is the main thread.
    DASSERT(pthread_equal(Thread::mainThread, pthread_self()), "");

    ASSERT((errno = pthread_barrier_init(&barrier, 0,
                numThreads + 1)) == 0, "");

    streams.push_back(this);
    DSPEW("now there are %d Streams", streams.size());
}

#if 0
Stream::wait(void)
{
    // This is the main thread.
    DASSERT(pthread_equal(Thread::mainThread, pthread_self()), "");

    DSPEW();

    bool threadRunning = true;

    while(threadRunning)
    {
        threadRunning = false;
        // First we wait for threads to finish running.
        for(auto thread: threads)
        {
            DASSERT(thread, "");
            MUTEX_LOCK(&thread->mutex);

            if(thread->filterModule)
            {
                // this thread is running or about to run.
                DSPEW("waiting for thread % " PRIu32 
                        " filter \"%s\" to return",
                        thread->threadNum,
                        thread->filterModule->name.c_str());
                if(thread->threadWaiting)
                    // signal the thread that is waiting now.
                    // The flag thread->threadWaiting and the mutex guarantee
                    // that the thread is waiting now.
                    ASSERT((errno = pthread_cond_signal(&thread->cond))
                            == 0, "");
                    // The thread will wake up only after we release the threads
                    // mutex lock down below here.

                threadRunning = true;
                MUTEX_UNLOCK(&thread->mutex);
#ifndef DEBUG
                // Check them all if in debug mode
                break;
#endif
            }

            MUTEX_UNLOCK(&thread->mutex);
        }
        // TODO: sleep is a little kludgey.  We should get a signal
        // from the last thread instead.
        if(threadRunning)
        {
            const struct timespec ts =
            {
                1/*seconds*/, 100000/*nanoseconds*/
            };

            DSPEW("waiting to threads to finish");
            nanosleep(&ts, 0);
        }
    }
}
#endif


Stream::~Stream(void)
{
    // This is the main thread.
    DASSERT(pthread_equal(Thread::mainThread, pthread_self()), "");

    ASSERT((errno = pthread_barrier_destroy(&barrier)) == 0, "");

    // The thread destructor removes itself from the
    // threads list.
    for(auto tt = threads.begin();
            tt != threads.end();
            tt = threads.begin())
        // The thread will delete all the filter modules
        // that in-turn destroys the CRTSFilter in the filter
        // module.
        delete *tt;

    // Remove this from the streams list
    streams.remove(this);

    DSPEW("now there are %d Streams", streams.size());
}


void Stream::destroyStreams(void)
{
    // This is the main thread.
    DASSERT(pthread_equal(Thread::mainThread, pthread_self()), "");

    auto it = streams.begin();
    for(;it != streams.end(); it = streams.begin())
        delete (*it);
}


// Return false on success.
//
// Each Stream is a factory of filter modules.  stream->load()
// is how we make them.
bool Stream::load(const char *name, int argc, const char **argv)
{
    // This is the main thread.
    DASSERT(pthread_equal(Thread::mainThread, pthread_self()), "");

    FilterModule *m = 0;

    void *(*destroyFilter)(CRTSFilter *);

    CRTSFilter *crtsFilter = LoadModule<CRTSFilter>(name, "Filters",
            argc, argv, destroyFilter);

    if(!crtsFilter || !destroyFilter)
        return true; // fail

    m = new FilterModule(this, crtsFilter, destroyFilter, loadCount, name);

    this->insert(std::pair<uint32_t, FilterModule*>(loadCount, m));

    ++loadCount;

    return false; // success
}


// Return false on success.
//
// Each stream manages the connections of filter modules.
bool Stream::connect(uint32_t from, uint32_t to)
{
    // This is the main thread.
    DASSERT(pthread_equal(Thread::mainThread, pthread_self()), "");

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

    f->readers = (FilterModule**) realloc(f->readers,
            sizeof(FilterModule*)*(f->numReaders+1));
    ASSERT(f->readers, "realloc() failed");
    f->readers[f->numReaders] = t; // t is the reader from f

    f->readerIndexes = (uint32_t *) realloc(f->readerIndexes,
            sizeof(uint32_t)*(f->numReaders+1));
    ASSERT(f->readerIndexes, "realloc() failed");
    // We are the last channel in the "t" writer list
    f->readerIndexes[f->numReaders] = t->numWriters;

    // The "t" filter needs to point back to the "f" filter so that we can
    // see and edit this connection from the "f" or "t" side, like it's a
    // doubly linked list.  If not for editing this "connection list", we
    // would not need this t->writers[].
    t->writers = (FilterModule**) realloc(t->writers,
            sizeof(FilterModule*)*(t->numWriters+1));
    ASSERT(t->writers, "realloc() failed");
    t->writers[t->numWriters] = f; // f is the writer to t


    ++f->numReaders;
    ++t->numWriters;


    // Set this flag so we know there was at least one connection.
    haveConnections = true;


    DSPEW("Connected filter %s writes to %s",
            f->name.c_str(), t->name.c_str());



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
    errno = pthread_kill(Thread::mainThread, exitSignals[0]);
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
    // This is the main thread.
    DASSERT(pthread_equal(Thread::mainThread, pthread_self()), "");

    // Keep this function consistent with the argument parsing:

    if(uopt)
        printf("\n Unknown option: %s\n\n\n", uopt);

    printf(
        "\n"
        "  Usage: %s OPTIONS\n",
        argv0);

    printf(
"\n"
"    Run the Cognitive Radio Test System (CRTS) transmitter/receiver program.\n"
" Some -f options are required.  The filter stream is setup as the arguments are\n"
" parsed, so stuff happens as the command line options are parsed.\n"
"\n"
"    For you \"system engineers\" the term \"filter\" we mean software filter\n"
" [https://en.wikipedia.org/wiki/Filter_(software)], module component node, or the\n"
" loaded module code that runs and passes data to other loaded module code that\n"
" runs and so on.  Component and node were just to generic a term in a software\n"
" sense.  If you have a hard time stomaching this terminology consider that\n"
" sources and sinks are just filters with null inputs and outputs correspondingly.\n"
" The real reason maybe that the word \"component\" has more letters in it than\n"
" the word \"filter\".   Maybe we should have used the word \"node\"; no too\n"
" generic.  The most general usage the word filter implies a point in a flow, or\n"
" stream.  The words component and node do not imply this in the most general\n"
" usage; they have no associated flow.\n"
"\n"
"\n"
"\n"
"                   OPTIONS\n"
"\n"
"\n"
"   -c | --connect LIST              how to connect the loaded filters that are\n"
"                                    in the current stream\n"
"\n"
"                                       Example:\n"
"\n"
"                                              -c \"0 1 1 2\"\n"
"\n"
"                                    connect from filter 0 to filter 1 and from\n"
"                                    filter 1 to filter 2.  This option must\n"
"                                    follow all the corresponding FILTER options\n"
"                                    Arguments follow a connection LIST will be\n"
"                                    in a new Stream.  After this option and next\n"
"                                    filter option with be in a new different stream\n"
"                                    and the filter indexes will be reset back to 0.\n"
"                                    If a connect option is not given after an\n"
"                                    uninterrupted list of filter options than a\n"
"                                    default connectivity will be setup that connects\n"
"                                    all adjacent filters.\n"
"\n"
"\n"
"   -d | --display                   display a DOT graph via dot and imagemagick\n"
"                                    display program, before running the streams.\n"
"                                    This option should be after filter options in\n"
"                                    the command line.  Maybe make it the last option.\n"
"\n"
"\n"
"   -e | --exit                      exit the program.  Used if you just want to\n"
"                                    print the DOT graph after building the graph.\n"
"                                    Also may be useful in debugging your command\n"
"                                    line.\n"
"\n"
"\n"
"   -f | --filter FILTER [OPTS ...]  load filter module FILTER passing the OPTS ...\n"
"                                    arguments to the CRTS Filter constructor.\n"
"\n"
"\n"
"   -h | --help                      print this help and exit\n"
"\n"
"\n"

"   -p | --print FILENAME            print a DOT graph to FILENAME.  This should be\n"
"                                    after all filter options in the command line.  If\n"
"                                    FILENAME ends with .png this will write a PNG\n"
"                                    image file to FILENAME.\n"
"\n"
"\n"
"   -t | --thread LIST              run the LIST of filters in a separate thread.\n"
"                                   Without this argument option the program will run\n"
"                                   all filters modules in a single thread.\n"
"\n"
"\n");

    return 1; // return error status
}


// ref:
//   https://en.wikipedia.org/wiki/DOT_(graph_description_language)
//
// Print a DOT graph to filename or PNG image of a directed graph
// return false on success
bool Stream::printGraph(const char *filename)
{
    // This is the main thread.
    DASSERT(pthread_equal(Thread::mainThread, pthread_self()), "");

    DSPEW("Writing DOT graph to: \"%s\"", filename);
    FILE *f;

    if(!filename || !filename[0])
    {
        // In this case we run dot and display the images assuming
        // the program "display" from imagemagick is installed in
        // the users path.

        errno = 0;
        f = tmpfile();
        if(!f)
        {
            ERROR("tmpfile() failed");
            return true; // failure
        }

        bool ret = printGraph(f);
        if(ret)
        {
            fclose(f);
            return ret; // failure
        }

        fflush(f);
        rewind(f);

        pid_t pid = fork();
        if(pid == 0)
        {
            // I'm the child
            errno = 0;
            if(0 != dup2(fileno(f), 0))
            {
                WARN("dup2(%d, %d) failed", fileno(f), 0);
                exit(1);
            }
            DSPEW("Running dot|display");
            // Now stdin is the DOT graph file
            // If this fails there's nothing we need to do about it.
            execl("/bin/bash", "bash", "-c", "dot|display", (char *) 0);
            exit(1);
        }
        else if(pid >= 0)
        {
            // I'm the parent
            fclose(f);

            int status = 0;
            INFO("waiting for child display process", status);
            errno = 0;
            // We wait for just this child.
            if(pid == waitpid(pid, &status, 0))
                INFO("child display process return status %d", status);
            else
                WARN("child display process gave a wait error");
        }
        else
        {
            ERROR("fork() failed");
        }

        return false; // success, at least we tried so many thing can fail
        // that we can't catch all failures, like X11 display was messed
        // up.
    }

    size_t flen = strlen(filename);

    if(flen > 4 && (!strcmp(&filename[flen - 4], ".png") ||
            !strcmp(&filename[flen - 4], ".PNG")))
    {
        // Run dot and generate a PNG image file.
        //
        const char *pre = "dot -o "; // command to run without filename
        char *command = (char *) malloc(strlen(pre) + flen + 1);
        sprintf(command, "%s%s", pre, filename);
        errno = 0;
        f = popen(command, "w");
        if(!f)
        {
            ERROR("popen(\"%s\", \"w\") failed", command);
            free(command);
            return true; // failure
        }
        free(command);
        bool ret = printGraph(f);
        pclose(f);
        return ret;
    }
    
    // else
    // Generate a DOT graphviz file.
    //
    f = fopen(filename, "w");
    if(!f)
    {
        ERROR("fopen(\"%s\", \"w\") failed", filename);
        return true; // failure
    }
        
    bool ret = printGraph(f);
    fclose(f);
    return ret;
}


bool Stream::printGraph(FILE *f)
{
    DASSERT(f, "");

    uint32_t n = 0; // stream number

    fprintf(f,
            "// This is a generated file\n"
            "\n"
            "// This is a DOT graph file.  See:\n"
            "//  https://en.wikipedia.org/wiki/DOT_(graph_description_language)\n"
            "\n"
            "// There are %zu filter streams in this graph.\n"
            "\n", Stream::streams.size()
    );

    fprintf(f, "digraph FilterStreams {\n");

    for(auto stream : streams)
    {
        fprintf(f,
                "\n"
                "  // Stream index %d\n", n);

        for(auto pair : *stream)
        {
            FilterModule *filterModule = pair.second;

            char wNodeName[64]; // writer node name

            snprintf(wNodeName, 64, "f%" PRIu32 "_%" PRIu32, n,
                    filterModule->loadIndex);

            // example f0_1 [label="stdin(0)\n1"] for thread 1
            fprintf(f, "  %s [label=\"%s\n%" PRIu32 "\"];\n",
                    wNodeName,
                    filterModule->name.c_str(),
                    (filterModule->thread)?
                        (filterModule->thread->threadNum):0
                    );

            for(uint32_t i = 0; i < filterModule->numReaders; ++i)
            {
                char rNodeName[64]; // reader node name
                snprintf(rNodeName, 64, "f%" PRIu32 "_%" PRIu32, n,
                        filterModule->readers[i]->loadIndex);

                fprintf(f, "  %s -> %s;\n", wNodeName, rNodeName);
            }
        }


        ++n;
    }

    fprintf(f, "}\n");

    return false; // success
}



static void signalExitProgramCatcher(int sig)
{
    INFO("Caught signal %d waiting to cleanly exit", sig);

    // Deal with multi-stream
    // Let them finish the last/current loop

    for(auto stream : Stream::streams)
        stream->isRunning = false;
}


static inline void finishThreads(void)
{
    // Unless we wish to make this a select(2), poll(2), or epoll(2)
    // based service.  All the source filters need to have a separate
    // thread, because their write() calls can block waiting for data.
    // That's just how UNIX works, assuming we don't force them to be
    // based on non-blocking I/O, which would be too hard for our level
    // of filter module code writers.
    //
    // So we made the source filters write calls never return until they
    // have no more data, like an end of file condition.  Simple enough.
    // This will prevent an unexpected blocking read(2) call from
    // unintentionally completely blocking any multi-source streams.  We
    // assume that filter module code writers are not sophisticated enough
    // to understand how to code blocking and non-blocking read(2) calls.
    // The parallel source inputs will just work if they are in different
    // threads, irregardless of whither read calls are blocking or not.
    //
    // Broken case: if the running makes separate source filters in the
    // same thread.  One source filter could block the other source filter
    // from reading input data.  By default we avoid this case, but users
    // could force this broken case to happen.
    //
    // If this thread default thread grouping does not work the user
    // should specify one on the command line that will work.
    //
    for(auto stream : Stream::streams)
    {
        //
        // For each stream we make sure that all filter modules are part
        // of a thread group; filter modules that were not in a thread
        // group get put in a new thread together.
        //
        // Source filters cannot be put in the same thread as another
        // source filter.
        //
        Thread *newThread = 0;

        for(auto it : stream->map)
        {
            // it.second is a FilterModule
            if(!it.second->thread)
            {
                // It's not in a thread.
                //
                // Source filters cannot be put in the same thread as
                // another source filter.
                //
                if(!newThread || it.second->isSource())
                    newThread = new Thread(stream);

                DSPEW("%sfilter \"%s\" added to thread %" PRIu32,
                            (it.second->isSource())?"source ":"",
                            it.second->name.c_str(),
                            newThread->threadNum);

                // Add this Filtermodule to this thread
                newThread->addFilterModule(it.second);

                DSPEW("%sfilter \"%s\" added to thread %" PRIu32,
                        (it.second->isSource())?"source ":"",
                        it.second->name.c_str(),
                        newThread->threadNum);
            }
        }
    }
}


static int setDefaultStreamConnections(Stream* &stream)
{
    // default connections: 0 1   1 2   2 3   3 4   4 5  ...
    // default connections: 0 -> 1   1 -> 2   2 -> 3   3 -> 4   4 -> 5  ...

    DASSERT(stream->haveConnections == false, "");

    uint32_t from = 0;

    for(auto toFilterModule : *stream)
        if(toFilterModule.second->loadIndex > 0)
            if(stream->connect(from++, toFilterModule.second->loadIndex))
                return 1; // fail

    // We set this flag here in case there was just one filter and
    // not really any connections.
    stream->haveConnections = true;

    // It just so happens we only call this directly or indirectly from
    // parseArgs where we make a new stream after each time we set default
    // connections, so setting stream = 0 tells us that:
    stream = 0;

    return 0; // success
}


// We happened to have more than one command line option that prints a DOT
// graph. So we put common code here to keep things consistent.
static inline int doPrint(Stream* &stream, const char *filename = 0)
{
    if(stream)
    {
        DASSERT(!stream->haveConnections, "");
        if(setDefaultStreamConnections(stream))
            return 1; // failure
    }

    finishThreads();

    if(Stream::printGraph(filename))
        return 1; // failure

    return 0; // success
}


/////////////////////////////////////////////////////////////////////
// Parsing command line arguments
//
//  Butt ugly, but straight forward
/////////////////////////////////////////////////////////////////////
//
static int parseArgs(int argc, const char **argv)
{
    // Current stream as a pointer.  There are none yet.
    Stream *stream = 0;

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
                stream = new Stream;

            DSPEW("got optional arg filter:  %s", str.c_str());

            // Add the filter to the current stream in the command-line.
            if(stream->load(str.c_str(), Argc, Argv))
                return 1;

            continue;
        }

        if((!strcmp("-t", argv[i]) || !strcmp("--thread", argv[i])))
        {
            if(!stream)
            {
                ERROR("At command line argument \"%s\": No "
                        "filters have been given"
                        " yet for the current stream", argv[i]);
                return 1; // failure
            }

            ++i;

            Thread *thread = 0;

             while(i < argc && argv[i][0] >= '0' && argv[i][0] <= '9')
             {
                // the follow args are like:  0 1 2 ...
                uint32_t fi; // filter index.
                errno = 0;
                fi = strtoul(argv[i], 0, 10);
                if(errno || fi > 0xFFFFFFF0)
                    return usage(argv[0], argv[i]);

                auto it = stream->find(fi);
                if(it == stream->end())
                {
                    ERROR("Bad filter index: %" PRIu32, fi);
                    return usage(argv[0], argv[i-1]);
                }
                FilterModule *filterModule = it->second;

                DASSERT(filterModule, "");
                DASSERT(!filterModule->thread,
                        "filter module \"%s\" already has a thread",
                        filterModule->name.c_str());

                if(filterModule->thread)
                {
                    ERROR("filter module \"%s\" already has a thread",
                        filterModule->name.c_str());
                    return 1; // Failure
                }

                if(!thread)
                    thread = new Thread(stream);

                // This filterModule is a member of this thread group.
                thread->addFilterModule(filterModule);

                ++i;
            }

            if(!thread)
            {
                ERROR("At command line argument \"%s\": No "
                        "valid filter load indexes given", argv[i-1]);
                return usage(argv[0], argv[i-1]);
            }


            // TODO: check the order of the filters in the thread
            // and make sure that it can work in that order...
            continue;
        }


        if((!strcmp("-c", argv[i]) || !strcmp("--connect", argv[i])))
        {
            if(!stream)
            {
                ERROR("At command line argument \"%s\": No "
                        "filters have been given"
                        " yet for the current stream", argv[i]);
                return 1; // failure
            }

            ++i;

            // We read the "from" and "to" channel indexes in pairs
            while(i + 1 < argc &&
                    argv[i][0] >= '0' && argv[i][0] <= '9' &&
                    argv[i+1][0] >= '0' && argv[i+1][0] <= '9')
            {
                // the follow args are like:  0 1 1 2
                // We get them in pairs like: 0 1 and 1 2
                uint32_t from, to;
                errno = 0;
                from = strtoul(argv[i], 0, 10);
                if(errno || from > 0xFFFFFFF0)
                    return usage(argv[0], argv[i]);
                ++i;
                to = strtoul(argv[i], 0, 10);
                if(errno || to > 0xFFFFFFF0)
                    return usage(argv[0], argv[i]);
                // Connect filters in the current stream.
                if(stream->connect(from, to))
                    return usage(argv[0], argv[i]);
                ++i;
            }

            if(!stream->haveConnections)
            {
                if(setDefaultStreamConnections(stream))
                    return 1; // failure
                // setDefaultStreamConnections(stream) set stream = 0
            }
            else
            {
                // TODO: Add a check of all the connections in the stream
                // here.  This is the only place we need to make sure they
                // are connected in a way that make sense.
                //
                // We are done with this stream.  We'll make a new stream
                // if the argument options require it.
                stream = 0;
            }

            // Ready to make a another stream if more filters are added.
            // The global Stream::streams will keep the list of streams
            // for us.
            continue;
        }

        if(get_opt(str, Argc, Argv, "-p", "--print", argc, argv, i))
        {
            if(doPrint(stream, str.c_str()))
                return 1; // error
            continue;
        }

        if(!strcmp("-d", argv[i]) || !strcmp("--display", argv[i]))
        {
            ++i;
            if(doPrint(stream, str.c_str()))
                return 1; // error
            continue;
        }

        if(get_opt_double(rx_freq, "-f", "--rx-freq", argc, argv, i))
            // TODO: remove this if() block. Just need as example of
            // getting a double from command line argument option.
            continue;

        if(!strcmp("-e", argv[i]) || !strcmp("--exit", argv[i]))
        {
            DSPEW("Now exiting due to exit command line option");
            exit(0);
        }

        if(!strcmp("-h", argv[i]) || !strcmp("--help", argv[i]))
            return usage(argv[0]);

        return usage(argv[0], argv[i]);

        //++i;
    }

    if(stream)
    {
        DASSERT(!stream->haveConnections, "");
        setDefaultStreamConnections(stream);
    }

    // Figure out which filter modules have no writers, i.e. are sources.
    for(auto stream : Stream::streams)
        stream->getSources();

    finishThreads();

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


    if(parseArgs(argc, argv))
    {
        // We failed so cleanup
        //
        // There should be not much go'n yet just filter modules
        // loaded.  We just need to call their factory destructor's.

        Stream::destroyStreams();

        return 1; // return failure status
    }

    ///////////////////////////////////////////////////////////////////
    // TODO: Check that connections in the stream make sense ???
    ///////////////////////////////////////////////////////////////////


    ///////////////////////////////////////////////////////////////////
    // We make sure no two sources share the same thread.
    //
    // A dumb user can do that via the command line.
    //
    for(auto stream : Stream::streams)
        for(auto filterModule : stream->sources)
            // TODO: don't double search the lists.
            for(auto filterModule2 : stream->sources)
                if(filterModule != filterModule2 &&
                    filterModule->thread == filterModule2->thread)
                {
                    ERROR("The two source filters: \"%s\" and \"%s\""
                            " share the same thread %" PRIu32".\n\n",
                        filterModule2->thread->threadNum,
                        filterModule->name.c_str(),
                        filterModule2->name.c_str());

                    return 1; // error fail exit.
                }
    //
    ///////////////////////////////////////////////////////////////////

    DASSERT(Thread::createCount, "");

    {
        // We just use this barrier once at the starting of the threads,
        // so we use this stack memory to create and use it.
        pthread_barrier_t barrier;
        DSPEW("have %zu threads", Thread::getTotalNumThreads());
        ASSERT((errno = pthread_barrier_init(&barrier, 0,
                    Thread::getTotalNumThreads() + 1)) == 0, "");

        // Start the threads.
        for(auto stream : Stream::streams)
            for(auto thread : stream->threads)
                thread->launch(&barrier);

        MUTEX_LOCK(&Stream::mutex);

        // Now we wait for all threads to be running past this
        // barrier.
        BARRIER_WAIT(&barrier);

        ASSERT((errno = pthread_barrier_destroy(&barrier)) == 0, "");
    }

    // Testing segfault catcher by setting bad memory address
    // to an int value.
    //void *tret
    //*(int *) (((uintptr_t) tret) + 0xfffffffffffffff8) = 1;


    // Now all the thread in all stream are running past there barriers,
    // so they are initialized and ready to loop.

    
    /* NOTE: THIS IS VERY IMPORTANT
     *

  Without threads FilterModule::write() is the start of a long repeating
  stack of write calls.  If there is a threaded filter to write to
  FilterModule::write() will set that threads data and than signal that
  thread to call CRTSFilter::write(), else if there is no different thread
  FilterModule::write() will call CRTSFilter::write() directly.

  CRTSFilter::write() will call any number of CRTSFilter::writePush()
  calls which in turn call FilterModule::write().  The
  CRTSFilter::writePush() function is nothing more than a wrapper of
  FilterModule::write() calls, so one could say CRTSFilter::write() calls
  any number of FilterModule::write() calls.  In the software
  stack/flow/architecture we can consider the CRTSFilter::writePush()
  calls as part of the CRTSFilter::write() calls generating
  FilterModule::write() calls.


  The general sequence/stack of filter "write" calls will vary based on
  the partitioning of the threads from the command line.  For example with
  no threads, and assuming that all the filters "get there fill of data",
  the write call stack will traverse the directed graph that is the
  filter stream, growing until it reaches sink filters and than popping
  back to the branch CRTSFilter::write() to grow to the next sink filter.
  In this way each CRTSFilter::write() may be a branch point.


  filter           function
  ------    --------------------------

    0        FilterModule::write()

    0            CRTSFilter::write()

    1                FilterModule::write()

    1                        CRTSFilter::write()

    .                            .......


  With threads each thread will have a stack like this which grows until
  it hits another thread in a CRTSFilter::write() call or it hits a sink
  filter.


   TODO:  

   Looks like all filters in thread must not have the flow interrupted
   by another thread.  We need to add a check for that.


     */


        // This is the main thread and it has nothing to do except wait.
        // Stream::wait() returns the number of streams running.


   for(auto stream : Stream::streams)
        for(auto filterModule : stream->sources)
            // Above we checked that all source filters belong to a
            // particular thread.
            //
            // TODO: If the user overrode that and put two or more source
            // filters in the same thread than only the first source
            // filter in the thread will work.  We should make sure no two
            // sources share the same thread.
            //
            // Source filter modules will loop until they are done.  These
            // ModuleFilter::write() calls will trigger a call to
            // CRTSFilter::write() in their own threads.
            filterModule->write(0,0,0, true);

    while(Stream::wait());

    MUTEX_UNLOCK(&Stream::mutex);


    // This will try to gracefully shutdown the stream and join the rest
    // of the threads:
    //
    // TODO: But of course if any modules are using libuhd it may fuck
    // that up, and may exit when you try to gracefully shutdown.
    //
    Stream::destroyStreams();

    DSPEW("FINISHED");

    return 0;
}
