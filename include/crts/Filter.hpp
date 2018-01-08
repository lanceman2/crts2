#ifndef __Filter_h__
#define __Filter_h__

#include <inttypes.h>
#include <pthread.h>

#include <crts/MakeModule.hpp>

// FilterModule is a opaque module thingy that the user need not worry
// about much.  We just needed it to add stuff to the CRTSFilter that we
// wanted to do some lifting without being seen by the user interface.
// CRTSFilter is the particular users filter implementation that has a
// standard CRTSFilter interface.  FilterModule just adds more genetic
// code to the CRTSFilter making it a software plugin filter module which
// you string together to build a CRTS "Stream".
//
class FilterModule;

// The Unix philosophy encourages combining small, discrete tools to
// accomplish larger tasks.  Reference:
//
//       https://en.wikipedia.org/wiki/Filter_(software)#Unix
//
// A stream can be thought of as items on a conveyor belt being processed
// one at a time.  In our case in CRTSFilters are placed at points along
// the conveyor belt to transform the data stream as it flows.
// This idea of "stream" seems to fit what we are doing.  We assemble
// CRTSFilters along our stream.  Reference:
//
//       https://en.wikipedia.org/wiki/Stream_(computing)
//       https://en.wikipedia.org/wiki/Filter_(software)
//

// When these modules run the coding interfaces are such that we do not
// have to know if these modules run on separate threads or not.  Wither
// or not these modules run on different threads is decided at run time.
// This is a requirement so that run time optimization is possible.
//
// Sometimes things are so simple that running a single thread is much
// faster than running more than one thread; and sometimes processing in a
// module takes so long that multi-threading, and multi-buffering between,
// the modules is faster.  We can test different filter/thread topologies
// on the fly and the filter writer does not have to be concerned about
// threading, which is a run time decision.  The cost for such flexibility 
// is requiring a particular interface, the CRTSFilter object.
//
// TODO:  It may be possible to make parts of this stream have CRTSFilter
// modules running in different processes, not just threads.  This should
// be able to be added seamlessly at without changing the users CRTSFilter
// code.
//
//
//         CRTSFilter trade-offs and considerations:
//
// If unlocked thread looping process time, for more than one thread in
// the stream, is much greater than inter thread contention time per loop,
// then we will get better performance with multi-threading.  If one
// module thread is the bottle-neck (slower than all others) than there
// may be no benefit to multi-threading.
//
// We can put any number of adjacent modules in the same thread.
//
//
// This idea of "stream" seems to fit what we are doing.
// https://en.wikipedia.org/wiki/Filter_(computing)
// 
// Base class for crts_radio IO (stream) modules or or call them stream
// modules.
//
// We are also concerned with sources and sinks of these streams.
// A source of a stream is a CRTSFilter with no data being written to it.
// A sink of a stream is a CRTSFilter with no data being written from it.
//
// This is a stream like thing using only the exposed write() methods
// for each part of the software filter stream.
//
//
// A common filter stream processing interface to seamlessly provide
// runtime optimization of threading and process topology selection.
//
// Kind of like run time optimization of MPI HPC jobs.
//
class CRTSFilter
{
    public:
        
        // Function to write to this.
        //
        // This stream gets data when this is called by the "writer" and
        // in response may call this may call the reader->write().  This
        // is how data flows in this group of connected Filter objects.
        //
        // write() must call reader->write() when it wishes to push data
        // along the "stream" because the particular instance is the only
        // thing that knows what data is available to be pushed along.  A
        // CRTSFilter is a software filter with it's own ideas of what it
        // is doing.
        //
        // If this is a source (writer=0, below) this write() will be
        // called with buffer=0.
        //
        // In a sense this write() executes the stream.
        //
        // Clearly the writer (caller of this) dictates the buffer size.
        virtual ssize_t write(void *buffer, size_t bufferLen) = 0;

        virtual ~CRTSFilter(void);

        CRTSFilter(void);

    protected:

        // User interface to write to the next module in the stream.

        void writePush(void *buffer, size_t bufferLen);

        // Returns a locked buffer if this module has a reader that
        // is in a different thread.  This will recycle buffers.
        // This will block if we have the maximum number of buffers
        // in this circular buffer queue.
        void *getBuffer(size_t bufferLen);

        // Releases a buffer lock if this module has a different thread
        // than the module that wrote to this module.  The module may
        // hold more than one lock, so that adjacent buffers may be
        // compared without memory copies.
        void releaseBuffer(void *buffer);

        // Think how many total packages can we handle on the conveyor
        // belts, held at the packagers (writers), and held at the
        // receivers (readers).  This is the buffer queue that is between
        // all the modules that access (read or write) this buffer
        // queue.
        void setBufferQueueLength(uint32_t n);


        static const uint32_t defaultBufferQueueLength;


    private:

        // If reader is null (0) this is a stream sink.
        CRTSFilter *reader;

        void setThreaded(void);

        pthread_t thread; // if there is a thread


        // Filter modules connections are made with Input writer
        // and Output reader.
        //
        // reader reads what this module produces triggered by
        // writer writes to this module.
        //
        // TODO: have multiple readers and writers or forks in the stream
        // flow.
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
        // If writer is null (0) than this is a source.
        //
        CRTSFilter *writer;

        // Buffer length needed/requested by this module.
        uint32_t bufferQueueLength;


    // The FilterModule has to manage the CRTSFilter adding readers and
    // writers from between separate CRTSFilter objects.  May be better
    // than exposing methods that should not be used by CRTSFilter
    // implementers.  Because the user never knows what a FilterModule is
    // the API and ABI never changes when FilterModule changes, whereby
    // making this interface more stable.
    friend FilterModule;

};


#define CRTSFILTER_MAKE_MODULE(derived_class_name) \
    CRTS_MAKE_MODULE(CRTSFilter, derived_class_name)

#endif // #ifndef __Filter__
