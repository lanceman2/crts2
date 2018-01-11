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
// FilterModule is the hidden parts of CRTSFilter
class FilterModule
{
    public:

        FilterModule(void);
        ~FilterModule(void);

        CRTSFilter *filter; // users co-class

        void *(*destroyFilter)(CRTSFilter *);

        int loadIndex; // From FilterModules::loadCount

        // Filter modules connections are made with Input writer
        // and Output reader.
        //
        // reader reads what this module produces triggered by
        // writer writes to this module.
        //
        // TODO: have multiple readers and writers giving forks and merges 
        // in the stream flow.
        //
        //
        //     SINK, SOURCE, and INTERMEDIATE filters:
        //
        // If there is no reader than this is a output stream filter
        // (flow) terminator or SINK.  If there is no writer than this is
        // a in stream filter (flow) terminator or SOURCE.  If there is a
        // reader and a writer than this is a continuous flow, pass
        // through, flow restriction, general stream, or in general a
        // software INTERMEDIATE stream filter.
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
        CRTSFilter **readers, **writers;
        // This is a corresponding array of indexes that this filter
        // uses as channel numbers to read and write. 
        // i=readerIndexes[n] is the channel index that the reader filter
        // that we pushWrite(,,n) sees, when this filter calls write(,,i)
        uint32_t *readerIndexes;
        // The length of the reader arrays and the writer arrays:
        uint32_t numReaders, numWriters;
};
