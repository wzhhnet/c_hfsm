/*
 * A very simple utilities that exchange event asynchronously via another thread.
 *
 * Author wanch
 * Date 2022/11/23
 * Email wzhhnet@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef UTILS_ERRORS_H
#define UTILS_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef true
#define true     1
#endif

#ifndef false
#define false    0
#endif

#ifndef NULL
#define NULL    ((void*)0)
#endif

#ifndef VOID
#define VOID
#endif

/** Common error code 
  */
#define    UTILS_SUCC              (0)
#define    UTILS_ERR_PTR           (-1)
#define    UTILS_ERR_PARAM         (-2)
#define    UTILS_ERR_MALLOC        (-3)
#define    UTILS_ERR_THREAD        (-4)
#define    UTILS_ERR_POOL_MEM      (-5)
#define    UTILS_ERR_POOL_SIZE     (-6)
#define    UTILS_ERR_POOL_FULL     (-7)
#define    UTILS_ERR_POOL_FREE     (-8)
#define    UTILS_ERR_POOL_ALLOC    (-9)

#define RETURN_IF_FAIL(ret, code)   \
    do {                            \
        if (ret != 0)               \
            return code;            \
    } while (0)

#define RETURN_IF_NULL(ptr, code)   \
    do {                            \
        if (ptr == NULL)            \
            return code;            \
    } while (0)

#define RETURN_IF_TRUE(cond, code)  \
    do {                            \
        if (cond)                   \
            return code;            \
    } while (0)

#define SIZEOF_ARRAY(arr) (sizeof(arr)/sizeof((arr)[0]))

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif  /*!< UTILS_ERRORS_H */

