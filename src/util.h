#ifndef _UTIL_H_
#define _UTIL_H_

#ifdef __cplusplus
#define EXTERN_C       extern "C"
#define EXTERN_C_BEGIN extern "C" {
#define EXTERN_C_END   }
#else
#define EXTERN_C       extern
#define EXTERN_C_BEGIN
#define EXTERN_C_END
#endif

#ifndef __cplusplus
#include <stdint.h>
typedef int32_t bool;
#define true    1
#define false   0

#ifndef offsetof
#define offsetof(type, field) ((long) &((type *)0)->field)
#endif /* offsetof */
#endif /* __cplusplus */

// return code
#define RC_SUCCESS  0 
#define RC_FAIL    -1

// exception processor
#define TRY( cond ) if( cond ) { goto _label_catch_end; }

#define TRY_GOTO( cond, goto_label ) if( cond ) { goto goto_label; }

#define CATCH( catch_label )  goto _label_catch_end; \
  catch_label:

#define CATCH_END _label_catch_end:
#endif /* _UTIL_H_ */
