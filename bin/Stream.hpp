// A factory of FilterModule class objects.
// We keep a list (map) in Stream.
class Stream : public std::map<uint32_t, FilterModule*>
{
    public:

        Stream(void);
        ~Stream(void);

        // For each Stream there is at least one source but there
        // can be any number of sources.  There we keep a list
        // of all the sources for this stream.
        std::list<FilterModule*> sources;

        std::map<uint32_t, FilterModule*> &map;

        void getSources(void);


        bool load(const char *name, int argc, const char **argv);

        bool connect(uint32_t from, uint32_t to);

        std::atomic<bool> isRunning;

    private:

        // Never decreases.  Incremented with each new FilterModule.
        uint32_t loadCount;
};
