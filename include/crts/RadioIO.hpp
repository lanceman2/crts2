#ifndef __RadioIO_h__
#define __RadioIO_h__

#include <crts/MakeModule.hpp>


// Base class for crts_radio IO modules.
class CRTSRadioIO
{
    public:

        virtual ssize_t write(const void *buffer, size_t bufferLen) = 0;

        virtual ~CRTSRadioIO(void);
        
        CRTSRadioIO(void);

        CRTSRadioIO *next;
};


#define CRTSRadioIO_MAKE_MODULE(derived_class_name) \
    CRTS_MAKE_MODULE(CRTSRadioIO, derived_class_name)

#endif // #ifndef __RadioIO__
