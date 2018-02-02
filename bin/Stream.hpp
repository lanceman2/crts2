class Thread;

// A factory of FilterModule class objects.
// We keep a list (map) in Stream.
//
// The FilterModules numbered by load index starting at 0
// in the order given on the crts_radio command line.
// That load index is the map key to find the FilterModule.
//
class Stream : public std::map<uint32_t, FilterModule*>
{
    public:

        // number of Thread objects running this stream.
        // Added to in each Thread::Thread() call.
        uint32_t numThreads;

        // For each Stream there is at least one source but there
        // can be any number of sources.  There we keep a list
        // of all the sources for this stream.
        std::list<FilterModule*> sources;

        // This is just a handy reference to the base class map,
        // so it is the correct class type to access the std:map
        // methods.  It makes some of the code less ugly.
        std::map<uint32_t, FilterModule*> &map;

        void getSources(void);


        bool load(const char *name, int argc, const char **argv);

        bool unload(FilterModule* filtermodule);

        bool connect(uint32_t from, uint32_t to);

        std::atomic<bool> isRunning;

        // Stream factory
        Stream(void);

        // list of all streams created
        static std::list<Stream*> streams;

        // Factory destructor
        static void destroyStreams(void);

        // Print a DOT graph file that represents all the streams
        // in the streams list.  Prints a PNG file if filename
        // ends in ".png".
        static bool printGraph(const char *filename = 0);


        // It removes itself from the streams list
        ~Stream(void);

        // We set this, haveConnections, flag if the --connect (or -c)
        // argument was given with the connection list (like -c 0 1 1 2)
        // then this is set.  This is so we can know to setup default
        // connections.
        bool haveConnections; // Flag standing for having any connections
        // at all.


        // TODO: Check this out ...
        // A lock for editing the this stream.
        pthread_rwlock_t rwlock;


        // A list of threads that run a list of filter modules.
        std::list<Thread *> threads;


        // So we may call wait()
        static pthread_cond_t cond;
        static pthread_mutex_t mutex;


        // Waits until a stream and all it's threads is cleaned up.
        // Returns the number of remaining running streams.
        static size_t wait(void);


    private:

        static bool printGraph(FILE *f);

        // Never decreases.  Incremented with each new FilterModule.
        uint32_t loadCount;


        // Barrier used at stream stop.  So we know when all
        // threads in this stream have gotten out of their running loop.
        //
        // TODO: use it to stop and start the stream so that the stream
        // can be stopped edited and then started.  Currently there is a
        // global barrier that the start all all threads in all streams.
        //
        pthread_barrier_t barrier;
 };
