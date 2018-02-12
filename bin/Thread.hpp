//
// It groups filters with a running thread.  There is
// a current filter that the thread is calling CRTSFilter::write()
// with and may be other filters that will be used.
//
// There can be many filter modules associated with a given Thread.
// This is just a wrapper of a pthread and it's associated thread
// synchronization primitives, and a little more stuff.
class Thread
{
    public:

        // Launch the pthread via pthread_create()
        //
        // We separated starting the thread from the constructor so that
        // the user and look at the connection and thread topology before
        // starting the threads.  The thread callback function is called
        // and all the threads in all streams will barrier at the top of
        // the thread callback.
        //
        // barrier must be initialized to the correct number of threads
        // plus any number of added barrier calls.
        //
        // This call doe not handle mutex or w/r locking.
        //
        void launch(pthread_barrier_t *barrier);

        inline void addFilterModule(FilterModule *f)
        {
            DASSERT(f, "");
            filterModules.push_back(f);
            f->thread = this;
        };

        // This call doe not handle mutex or w/r locking.
        //
        inline size_t removeFilterModule(FilterModule *f)
        {
            DASSERT(f, "");
            filterModules.push_back(f);
            f->thread = 0;
            delete f;
            return filterModules.size();
        };

        // Returns the total number of existing thread objects in all streams.
        static inline size_t getTotalNumThreads(void)
        {
            return totalNumThreads;
        }


        Thread(Stream *stream);

        // This will pthread_join() after setting a running flag.
        ~Thread();

        pthread_t thread;
        pthread_cond_t cond;
        pthread_mutex_t mutex;


        // We let the main thread be 0 and this starts at 1
        // This is fixed after the object creation.
        // This is mostly so we can see small thread numbers like
        // 0, 1, 2, 3, ... 8 whatever, not like 23431, 5634, ...
        uint32_t threadNum;

        // Number of these objects created.
        static uint32_t createCount;

        static pthread_t mainThread;

        // This barrier gets passed at launch().
        pthread_barrier_t *barrier;

        // stream is the fixed/associated Stream which contains all filter
        // modules, some may not be in the Thread::filtermodules list.  A
        // stream can have many threads.  threads are not shared across
        // Streams.
        //
        Stream &stream;

        // At each loop we may reset the buffer, len, and channel
        // to call filterModule->filter->write(buffer, len, channel);

        /////////////////////////////////////////////////////////////
        //       All Thread data below here is changing data.
        //       We must have the mutex just above to access these:
        /////////////////////////////////////////////////////////////

        // These may be setup by another thread that is feeding data to
        // this thread via Filtermodule::write(), the memory of these
        // are in the Filtermodule::write() stack.  We use them to
        // pop the write queue.
        //
        pthread_mutex_t *queueMutex;
        pthread_cond_t *queueCond;


        // We have two cases, blocks of code, when this thread is not
        // holding the mutex lock:
        //
        //       1) in the block that calls CRTSFilter::write()
        //
        //       2) in the pthread_cond_wait() call
        //
        // We need this flag so we know which block of code the thread is
        // in from the main thread, otherwise we'll call
        // pthread_cond_signal() when it is not necessary.
        //
        //
        // TODO: Does calling pthread_cond_signal() when there are no
        // listeners make a mode switch, or use much system resources?
        // I would think checking a flag uses less resources than
        // calling pthread_cond_signal() when there is no listener.
        // It adds a function call or two to the stack and that must be
        // more work than checking a flag.  If it adds a mode switch than
        // this flags adds huge resource savings.
        //
        bool threadWaiting; // thread calling pthread_cond_wait()



        // All the filter modules that this thread can call
        // CRTSFilter::write() for.  Only this thread pthread is allowed
        // to change this list after the startup barrier.  This list may
        // only be changed if this thread has the stream::mutex lock in
        // addition to the this objects Thread mutex lock.
        std::list<FilterModule*> filterModules;


        bool hasReturned; // the thread returned from its callback

        // The Filter module that will have it's CRTSFilter::write() called
        // next.  Set to 0 if this is none.
        FilterModule *filterModule;

        // buffer, len, channelNum are for calling 
        // CRTSFilter::write(buffer, len, channelNum)
        //
        void *buffer;

        // Buffer length
        size_t len;

        // Current channel to write.
        uint32_t channelNum;

    private:

        static size_t totalNumThreads; // total all threads in all streams.
};
