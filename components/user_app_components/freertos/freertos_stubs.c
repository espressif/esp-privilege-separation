#include <freertos/FreeRTOS.h>

#if ( configUSE_NEWLIB_REENTRANT == 1 )
struct _reent* __getreent(void) {
    return NULL;
}
#endif
