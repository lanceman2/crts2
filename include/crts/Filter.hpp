#ifndef __Filter_h__
#define __Filter_h__

#include <inttypes.h>
#include <pthread.h>
#include <list>

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
// runtime optimization of threading and process topology selection.  Kind
// of like in optimization of MPI (massage passing interface) HPC (high
// performance computing) applications, we have node/thread runtime
// partitioning.
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
        //
        // channelNum is set to non-zero values in order to merge filter
        // streams.  Most CRTSFilters will not care about channelNum.
        //
        // 0 <= channelNum < N   It's up to the CRTSFilter to decide what
        // to do with channelNum.  A CRTSFilter code that looks at
        // channelNum may be a stream merging filter, or a general stream
        // switching filter.
        virtual
        ssize_t write(
                void *buffer,
                size_t bufferLen,
                uint32_t channelNum=0) = 0;

        virtual ~CRTSFilter(void);

        CRTSFilter(void);

    protected:

        // User interface to write to the next module in the stream.

        // The CRTSFilter code know if they are pushing to more than on
        // channel.  The channel we refer to here is just an attribute of
        // this filter point (node) in this stream.  ChannelNum goes from
        // 0 to N-1 where N is the total CRTSFilters that are connected
        // to push this data to.  It's up to the writer of the CRTSFilter
        // to define how to push to each channel, and what channel M (0 <=
        // M < N) means.  Do not confuse this with other channel
        // abstractions like a frequency sub band or other software
        // signal channel.  Use channelNum=0 unless this CRTSFilter is
        // splitting the stream.
        //
        // TODO: Named channels and other channel mappings that are not
        // just a simple array index thing.
        //
        // TODO: Varying number of channels on the fly.  This may just
        // work already.
        //
        // If a channel is not "connected" this does nothing or fails.
        //
        // writePush() may be called many times in a given write() call.
        // writePush() manages its self keeping track of how many channels
        // there are just from the calls to writePush(), there is no need
        // to create channels explicitly, but we can manage channels
        // explicitly too.
        //
        // channelNum handles the splitting of the stream.  We can call
        // writePush() many times to write the same thing to many
        // channels; with the cost only being an added call on the
        // function stack.
        void writePush(void *buffer, size_t len, uint32_t channelNum = 0);


        // Returns a locked buffer if this module has a reader that
        // is in a different thread.  This will recycle buffers.
        // This will block if we have the maximum number of buffers
        // in this circular buffer queue.
        void *getBuffer(size_t bufferLen);

        // Releases a buffer lock if this module has a different thread
        // than the module that wrote to this module.  The module may
        // hold more than one lock, so that adjacent buffers may be
        // compared without memory copies.
        void releaseBuffer(void *buffer, ssize_t nWritten);

        // Think how many total packages can we handle on the conveyor
        // belts, held at the packagers (writers), and held at the
        // receivers (readers).  This is the buffer queue that is between
        // all the modules that access (read or write) this buffer
        // queue.
        //
        // Think: What is the maximum number of packages that will fit on
        // the conveyor belt.
        void setBufferQueueLength(uint32_t n);


    private:

        static const uint32_t defaultBufferQueueLength;

        // Buffer length needed/requested by this module.
        uint32_t bufferQueueLength;


    // The FilterModule has to manage the CRTSFilter adding readers and
    // writers from between separate CRTSFilter objects.  May be better
    // than exposing methods that should not be used by CRTSFilter
    // implementers, because the user never knows what a FilterModule is
    // the API and ABI never changes when FilterModule changes, whereby
    // making this interface more stable.
    friend FilterModule;

};


#define CRTSFILTER_MAKE_MODULE(derived_class_name) \
    CRTS_MAKE_MODULE(CRTSFilter, derived_class_name)

#endif // #ifndef __Filter__
