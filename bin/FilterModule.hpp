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
        void write(void *buffer, size_t len, uint32_t channelNum);


        // The buffers that this filter module is using in a given
        // CRTSFilter::write() call.
        std::stack<void *>buffers;


        // the thread that this filter module is running in.
        ThreadGroup *threadGroup;



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
// There can be many filter modules associated with a given ThreadGroup.
// This is just a wrap of a pthread and it's associated thread
// synchronization primitives, and a little more stuff.
class ThreadGroup
{
    public:

        void run(FilterModule *filterModule);

        ThreadGroup(Stream *stream);
        ~ThreadGroup();

        pthread_t thread;
        pthread_cond_t cond;
        pthread_mutex_t mutex;

        // The Filter that will have it's CRTSFilter::write() called
        // next.
        FilterModule *filterModule;

        // Current channel to write.
        uint32_t channel;

        // The buffer that this filter module needs use when calling
        // CTRSFilter::write() to.
        Buffer *buffer;


        // stream is the fixed/associated Stream which contains
        // any filter modules that may be involved.
        Stream &stream;
};
