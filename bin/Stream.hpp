// A factory of FilterModule class objects.
// We keep a list (map) in Stream.
//
class Stream;
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
        static Stream *createStream(void);

        static std::list<Stream*> streams;

        // Factory destructor
        static void destroyStreams(void);

        // It removes itself from the streams list
        ~Stream(void);

        // We set this flag if the --connect (or -c) argument was given
        // with the connection list (like -c 0 1 1 2) then this is set.
        // This is so we can know to setup default connections.
        bool checkedConnections;

    private:

        Stream(void);

        // Never decreases.  Incremented with each new FilterModule.
        uint32_t loadCount;
};
