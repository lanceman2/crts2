#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string>
#include <uhd/usrp/multi_usrp.hpp>

#include "crts/debug.h"
#include "crts/Filter.hpp"
#include "crts/crts.hpp" // for:  FILE *crtsOut

#include "usrp_set_parameters.hpp" // UHD usrp wrappers
#include "defaultUSRP.hpp" // defaults: RX_FREQ, RX_RATE, RX_GAIN


// RANT:
//
// It'd be real nice if the UHD API would document what is thread-safe and
// what is not for all the API.  We can only guess how to use this stupid
// UHD API by looking at example codes.  From the program crashes I've
// seen there are clearly some things that are not thread safe.
//
// The structure of the UHD API implies that you should be able to use a
// single uhd::usrp::multi_usrp::sptr to do both transmission and
// receiving but none of the example do that, the examples imply that you
// must make two uhd::usrp::multi_usrp::sptr objects one for (TX)
// transmission and one for (RX) receiving.

// register UHD message handler
// Let it use stdout, or stderr by default???
//uhd::msg::register_handler(&uhd_msg_handler);

// We do not know where UHD is thread safe, so, for now, we do this
// before we make threads.  The UHD examples do it this way too.
// We set up the usrp (RX and TX) objects in the main thread here:


// UHD BUG WORKAROUND:
//
// We must make the two multi_usrp objects before we configure them by
// setting frequency, rate (bandWidth), and gain; otherwise the process
// exits with status 0.  And it looks like you can use the same object for
// both receiving (RX) and transmitting (TX).   Here we are keeping a list
// of stupid things libuhd does, and a good API will never do:
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
// It may be libBOOST is doing some of this.
//
// We sometimes get Floating point exception and the program exits.


class Rx : public CRTSFilter
{
    public:

        Rx(int argc, const char **argv);
        ~Rx(void);

        ssize_t write(void *buffer, size_t bufferLen,
                uint32_t channelNum);
    private:

        void init(void);

        uhd::usrp::multi_usrp::sptr usrp;
        uhd::device::sptr device;
        size_t numComplexFloats;
        std::string uhd_args;
        double freq, rate, gain;
};


// This is called if the user ran something like: 
//
//    crts_radio -f file [ --help ]
//
//
static void usage(void)
{
    char name[64];
    fprintf(stderr,
"\n"
"\n"
"Usage: %s [ OPTIONS ]\n"
"\n"
"  OPTIONS are optional.\n"
"\n"
"  As an example you can run something like this:\n"
"\n"
"       crts_radio -f rx [ --uhd addr=192.168.10.3 --freq 932 ] -f stdout\n"
"\n"
"\n"
"  ---------------------------------------------------------------------------\n"
"                           OPTIONS\n"
"  ---------------------------------------------------------------------------\n"
"\n"
"\n"
"   --uhd ARGS      set the arguments to give to the uhd::usrp constructor.\n"
"\n"
"                                 Example: %s [ --uhd addr=192.168.10.3 ]\n"
"\n"
"                   will use the USRP (Universal Software Radio Peripheral)\n"
"                   which is accessible at Ethernet IP4 address 192.168.10.3\n"
"\n"
"\n"
"   --freq FREQ     set the initial receiver frequency to FREQ MHz.  The default\n"
"                   initial receiver frequency is %g MHz.\n"
"\n"
"\n"
"   --gain GAIN     set the initial receiver gain to GAIN.  The default initial\n"
"                   receiver gain is %g.\n"
"\n"
"\n"
"   --rate RATE     set the initial receiver sample rate to RATE million samples\n"
"                   per second.  The default initial receiver rate is %g million\n"
"                   samples per second.\n"
"\n"
"\n"
"\n",
        CRTSFILTER_NAME(name, 64),
        CRTSFILTER_NAME(name, 64),
        RX_FREQ, RX_GAIN, RX_RATE);

    errno = 0;
    throw "usage help"; // This is how return an error from a C++ constructor
    // the module loader will catch this throw.
}


static double getDouble(const char *str)
{
    char name[64];
    double ret;
    char *ptr = 0;
    errno = 0;

    ret = strtod(str, &ptr);
    if(ptr == str || errno)
    {
        fprintf(crtsOut, "\nBad module %s arg: %s\n\n",
                CRTSFILTER_NAME(name, 64), str);
        usage();
    }

    return ret;
}



Rx::Rx(int argc, const char **argv):
    usrp(0), device(0), uhd_args(""),
    freq(RX_FREQ), rate(RX_RATE), gain(RX_GAIN)
{
    int i;
#ifdef DEBUG
    DSPEW();
    if(argc>0)
        DSPEW("  GOT ARGS");
    for(i=0; i<argc; ++i)
        DSPEW("    ARG[%d]=\"%s\"", i, argv[i]);
#endif

    for(i=0; i<argc; ++i)
    {
        if(!strcmp(argv[i], "--uhd") && i<argc+1)
        {
            uhd_args = argv[++i];
            continue;
        }
        if(!strcmp(argv[i], "--freq") && i<argc+1)
        {
            freq = getDouble(argv[++i]);
            continue;
        }
        if(!strcmp(argv[i], "--rate") && i<argc+1)
        {
            rate = getDouble(argv[++i]);
            continue;
        }
        if(!strcmp(argv[i], "--gain") && i<argc+1)
        {
            gain = getDouble(argv[++i]);
            continue;
        }

        usage();
    }

    // Convert the rate and freq to Hz from MHz
    freq *= 1.0e6;
    rate *= 1.0e6;

    // This init() call fails.  We think because is is not running in the
    // same thread that is reading the RX, or maybe it's just not reading
    // soon enough.
    //init();
}


void Rx::init(void)
{
    usrp = uhd::usrp::multi_usrp::make(uhd_args);

    crts_usrp_rx_set(usrp, freq, rate, gain);

    DSPEW("usrp->get_pp_string()=\n%s",
            usrp->get_pp_string().c_str());
    DSPEW("usrp->get_rx_num_channels()=%d",
            usrp->get_rx_num_channels());

    //setup streaming. Whatever that means.
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    // TODO: what does this return?
    usrp->issue_stream_cmd(stream_cmd);

    device = usrp->get_device();

    numComplexFloats = device->get_max_recv_samps_per_packet();
    DSPEW("RX numComplexFloats = %zu", numComplexFloats);
}


Rx::~Rx(void)
{
    DSPEW();

    // TODO: What does this return:
    if(usrp)
        usrp->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

    // TODO: delete usrp device ????

    DSPEW();
}


ssize_t Rx::write(void *buffer_in, size_t len, uint32_t channelNum)
{
    // This filter is a source so there no data passed to
    // whatever called this write()
    //
    // TODO:  or we could just ignore the input buffer??
    DASSERT(buffer_in == 0, "");

    // This init() call creates libuhd resources that must be in this
    // thread, because of libuhd.
    if(!device) init();



    std::complex<float> *buffer = (std::complex<float> *)
            getBuffer(sizeof(std::complex<float>)*numComplexFloats);

    uhd::rx_metadata_t metadata; // set by recv();

    size_t numSamples = device->recv(
            (unsigned char *)buffer, numComplexFloats, metadata,
            uhd::io_type_t::COMPLEX_FLOAT32,
            uhd::device::RECV_MODE_ONE_PACKET,
            // TODO: fix this timeout ??
            1.0/*timeout double seconds*/);

#ifdef DEBUG
    if(numSamples != numComplexFloats)
        DSPEW("RX recv metadata.error_code=%d numSamples = %zu",
                metadata.error_code, numSamples);
#endif

    if(metadata.error_code && metadata.error_code !=
            uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
    {
        DSPEW("RX recv metadata.error_code=%d numSamples = %zu",
                metadata.error_code, numSamples);
        // For error codes see:
        // https://files.ettus.com/manual/structuhd_1_1rx__metadata__t.html#ae3a42ad2414c4f44119157693fe27639
        DSPEW("uhd::rx_metadata_t::ERROR_CODE_NONE=%d",
                uhd::rx_metadata_t::ERROR_CODE_NONE);
        DSPEW("uhd::rx_metadata_t::ERROR_CODE_TIMEOUT=%d",
                uhd::rx_metadata_t::ERROR_CODE_TIMEOUT);
    }

    DASSERT(!(metadata.error_code && numSamples), "");

    if(numSamples > 0)
        writePush(buffer, numSamples*sizeof(std::complex<float>),
                CRTSFilter::ALL_CHANNELS);

    return 1; // TODO: what to return????
}


// Define the module loader stuff to make one of these class objects.
CRTSFILTER_MAKE_MODULE(Rx)
