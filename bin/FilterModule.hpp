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
//

class Stream;
class ThreadGroup;
class CRTSFilter;



// FilterModule is the hidden parts of CRTSFilter
class FilterModule
{
    public:

        FilterModule(Stream *stream, CRTSFilter *filter,
                void *(*destroyFilter)(CRTSFilter *), int32_t loadIndex,
                std::string name);

        ~FilterModule(void);

        CRTSFilter *filter; // users co-class

        void *(*destroyFilter)(CRTSFilter *);

        int loadIndex; // From Stream::loadCount

        // Filter modules connections are made with Input writer
        // and Output reader.
        //
        // reader reads what this module produces triggered by
        // writer writes to this module.
        //
        // We have multiple readers and writers giving forks and merges 
        // in the stream flow.
        //
        //
        //     SINK, SOURCE, and INTERMEDIATE filters:
        //
        // If there is no reader than this is a output stream filter
        // (flow) terminator or SINK stream filter.  If there is no writer
        // than this is a in stream filter (flow) terminator or SOURCE
        // stream filter.  If there is a reader and a writer than this is
        // a continuous flow, pass through, flow restriction, general
        // stream, or in general a software INTERMEDIATE stream filter.
        //
        // We hide the private data and functions in class FilterModule
        // so as to not pollute this interface header file.

        // TODO: Consider a more rapidly nodal topology and make a general
        // node class that better handles connection changes.
        //
        // Indexing into arrays is fast and changing connections it slow,
        // so we are not planing on changing connection often.  We can
        // change the method we use to connect filters without affecting
        // the CRTSFilter user interface.

        // We assume (for now) that the connections are pretty static
        // so accessing the adjacent filters via an array is fine.
        //
        // If fast changes are needed in this connectivity we can make
        // this a std::map later without affecting the user interface.
        // readers, writers are arrays of pointers:
        FilterModule **readers, **writers;
        // This is a corresponding array of indexes that this filter
        // uses as channel numbers to read and write. 
        // i=readerIndexes[n] is the channel index that the reader filter
        // that we pushWrite(,,n) sees, when this filter calls write(,,i)
        uint32_t *readerIndexes;
        // The length of the reader arrays and the writer arrays:
        uint32_t numReaders, numWriters;


        std::string name; // name from program crts_radio command line argv[]

 
        // Buffer length needed/requested by this module.
        uint32_t bufferQueueLength;

        // This write calls the underlying CRTSFilter::write() functions
        // or signals a thread that calls the underlying
        // CRTSFilter::write() function.
        void write(void *buffer, size_t len,
                uint32_t channelNum, bool toDifferentThread = false);


        // The buffers that this filter module is using in a given
        // CRTSFilter::write() call.
        std::stack<void *>buffers;


        // the thread that this filter module is running in.
        ThreadGroup *threadGroup;


        // Is set from the CRTSFilter constructor and stays constant.
        // Flag to say if this object can write to the buffer
        // in CRTSFilter::write(buffer,,).  If this is not set the
        // CRTSFIlter::write() function should not write to the buffer
        // that is passed in the arguments.
        bool canWriteBufferIn;


        void removeUnusedBuffers(void);


    friend CRTSFilter; // CRTSFilter and FilterModule are co-classes
    // (sister classes); i.e.  they share their data and methods.  We just
    // made them in two classes so that we could add code to FilterModule
    // without the users CRTSFilter inferface seeing changes to the
    // FilterModule interface.  Call it interface hiding where the
    // FilterModule is the hidden part of CRTSFilter.
    friend Stream;
};


struct Buffer;

// There are no ThreadGroup objects is there was no
// --thread command-line options (or equivalent)
//
// It groups filters with a running thread.
//
// There can be many filter modules associated with a given ThreadGroup.
// This is just a wrap of a pthread and it's associated thread
// synchronization primitives, and a little more stuff.
class ThreadGroup
{
    public:

        // Launch the pthread via pthread_create()
        // We separated starting the thread from the constructor so that
        // the user and look at the connection and thread topology before
        // starting the threads.
        void run(void);

        ThreadGroup(Stream *stream);

        // This will pthread_join() after setting a running flag.
        ~ThreadGroup();

        pthread_t thread;
        pthread_cond_t cond;
        pthread_mutex_t mutex;


        // We let the main thread be 0 and this starts at 1
        // This is fixed after the object creation.
        // This is mostly so we can see small thread numbers like
        // 0, 1, 2, 3, ... 8 whatever, not shit like 23431, 5634, ...
        uint32_t threadNum;

        // Number of these objects created.
        static uint32_t createCount;

        static pthread_t mainThread;
        static pthread_barrier_t *barrier;

        // stream is the fixed/associated Stream which contains
        // any filter modules that may be involved.
        Stream &stream;

        // At each loop we may reset the buffer, len, and channel
        // to call filterModule->filter->write(buffer, len, channel);

        /////////////////////////////////////////////////////////////
        //       All ThreadGroup data below here is changing data.
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
};
