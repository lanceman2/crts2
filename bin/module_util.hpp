
// A thread-safe path finder
extern char *GetPluginPath(const char *category, const char *name);


template <class T>
T *LoadModule(const char *path, const char *category, int argc,
        const char **argv, void *(*&destroyer)(T *))
{
    DASSERT(path, "");
    DASSERT(category, "");
    DASSERT(&destroyer, "");

    destroyer = 0;
    T *ret = 0;


    path = GetPluginPath(category, path);
    // GCC bug: This DASSERT prints the wrong filename, because this
    // is a template function; __BASE_FILE__ is not module_util.hpp.
    // But __LINE__ is correct.  Not a show stopper, but makes debugging
    // harder.
    DASSERT(path, "");

    DSPEW("loading module: \"%s\"", path);

    ModuleLoader<T,T *(*)(int argc, const char **argv)> moduleLoader;

    if(moduleLoader.loadFile(path,
            RTLD_NODELETE|RTLD_GLOBAL|RTLD_NOW|RTLD_DEEPBIND))
    {
        ERROR("Failed to load module plugin \"%s\"", path);
        return 0;
    }


    try
    {
        ret = moduleLoader.create(argc, argv);
        ASSERT(ret, "");
        destroyer = moduleLoader.destroy;
        DASSERT(destroyer, "");
    }
    catch (const char *msg)
    {
        // This is what I hate about C++.
        //
        // Do I call the destructor or not?

        ERROR("Failed to load module: \"%s\"\n"
                "exception thrown: \"%s\"", path, msg);

        if(ret && destroyer)
            destroyer(ret);
        ret = 0;
        destroyer = 0;
    }
    catch(...)
    {
        ERROR("Failed to load module \"%s\""
                "an exception was thrown", path);

        if(ret && destroyer)
            destroyer(ret);
        ret = 0;
        destroyer = 0;
    }

    free((void *) path);

    return ret;
}
