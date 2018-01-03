#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "ModuleLoader.hpp"
#include "module_util.hpp"

// Some butt ugly character string counting count.


// The relative paths to crts_program and the module with name name is the
// same in the source and installation is like so:
//
//  program_path =
//  /home/lance/installed/crts-1.0b4/bin/crts_program
//
//  plugin_path =
//  /home/lance/installed/crts-1.0b4/share/crts/plugins/category/name
//
// in source tree:
//
//  /home/lance/git/crts/bin/crts_program
//  /home/lance/git/crts/share/crts/plugins/category/name
//
//
// similarly in a tarball source tree
//

// The returned buffer must be free-d via free().

#define PRE "/share/crts/plugins/"

// A thread-safe path finder looks at /proc/self which is the same as
// /proc/self.  Note this is linux specific code using the linux /proc/
// file system.
//
// The returned pointer must be free()ed.
char *GetPluginPath(const char *category, const char *name)
{
    DASSERT(strlen(name) >= 1, "");
    DASSERT(strlen(category) >= 1, "");

    // postLen = strlen("/share/crts/plugins/" + category + '/' + name)
    static const ssize_t postLen =
        strlen(PRE) + strlen(category) +
        strlen(name) + 5/* for '/' and ".so" and '\0' */;
    DASSERT(postLen > 0 && postLen < 1024*1024, "");
    ssize_t bufLen = 128 + postLen;
    char *buf = (char *) malloc(bufLen);
    ASSERT(buf, "malloc() failed");
    ssize_t rl = readlink("/proc/self/exe", buf, bufLen);
    ASSERT(rl > 0, "readlink(\"/proc/self/exe\",,) failed");
    while(rl + postLen >= bufLen)
    {
        DASSERT(bufLen < 1024*1024, ""); // it should not get this large.
        buf = (char *) realloc(buf, bufLen += 128);
        ASSERT(buf, "realloc() failed");
        rl = readlink("/proc/self/exe", buf, bufLen);
        ASSERT(rl > 0, "readlink(\"/proc/self/exe\",,) failed");
    }

    buf[rl] = '\0';
    // Now buf = path to program binary.
    //
    // Now strip off to after "/bin/crts_" (Linux path separator)
    // by backing up two '/' chars.
    --rl;
    while(rl > 5 && buf[rl] != '/') --rl; // one '/'
    ASSERT(buf[rl] == '/', "");
    --rl;
    while(rl > 5 && buf[rl] != '/') --rl; // two '/'
    ASSERT(buf[rl] == '/', "");
    buf[rl] = '\0'; // null terminate string

    // Now
    //
    //     buf = "/home/lance/installed/crts-1.0b4"
    // 
    //  or
    //     
    //     buf = "/home/lance/git/crts"

    // Now just add the postfix to buf.
    //
    // If we counted chars correctly strcat() should be safe.

    DASSERT(strlen(buf) + strlen(PRE) +
            strlen(category) + strlen("/") +
            strlen(name) + 1 < (size_t) bufLen, "");

    strcat(buf, PRE);
    strcat(buf, category);
    strcat(buf, "/");
    strcat(buf, name);

    // Reuse the bufLen variable.

    bufLen = strlen(buf);
    if(strcmp(&buf[bufLen-3], ".so"))
        strcat(buf, ".so");

    return buf;
}
