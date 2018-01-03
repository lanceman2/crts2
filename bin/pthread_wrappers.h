// Simple pthread_*() wrappers.


#define BARRIER_WAIT(barrier)                                           \
    do {                                                                \
        DSPEW("Enter barrier");                                         \
        int ret = pthread_barrier_wait(barrier);                        \
        ASSERT(ret == 0 || ret == PTHREAD_BARRIER_SERIAL_THREAD,        \
                "pthread_barrier_wait()=%d failed", ret);               \
        DSPEW("Exit barrier");                                          \
    } while(0)

#if 0
#  define MSPEW(x) DSPEW(x)
#else
#  define MSPEW(x) /*empty macro*/
#endif

#define MUTEX_LOCK(mutex)                                       \
    do {                                                        \
        MSPEW("Locking Mutex");                                 \
        errno = pthread_mutex_lock(mutex);                      \
        ASSERT(errno == 0, "pthread_mutex_lock() failed");      \
        MSPEW("Mutex Locked");                                  \
    } while(0)

#define MUTEX_UNLOCK(mutex)                                     \
    do {                                                        \
        MSPEW("Unlocking Mutex");                               \
        errno = pthread_mutex_unlock(mutex);                    \
        ASSERT(errno == 0, "pthread_mutex_unlock() failed");    \
        MSPEW("Mutex Unlocked");                                \
    } while(0)
