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

        // For each Stream there is at least one source but there
        // can be any number of sources.  There we keep a list
        // of all the sources for this stream.
        std::list<FilterModule*> sources;

        std::map<uint32_t, FilterModule*> &map;

        void getSources(void);


        bool load(const char *name, int argc, const char **argv);

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
        static bool printGraph(const char *filename);


        // It removes itself from the streams list
        ~Stream(void);

        // We set this flag if the --connect (or -c) argument was given
        // with the connection list (like -c 0 1 1 2) then this is set.
        // This is so we can know to setup default connections.
        bool checkedConnections;

    private:

        static bool printGraph(FILE *f);

        // Never decreases.  Incremented with each new FilterModule.
        uint32_t loadCount;
};
