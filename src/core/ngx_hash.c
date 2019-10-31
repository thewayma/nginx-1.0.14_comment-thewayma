
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


void *
ngx_hash_find(ngx_hash_t *hash, ngx_uint_t key, u_char *name, size_t len)
{
    ngx_uint_t       i;
    ngx_hash_elt_t  *elt;

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "hf:\"%*s\"", len, name);
#endif

    elt = hash->buckets[key % hash->size];

    if (elt == NULL) {
        return NULL;
    }

    while (elt->value) {
        if (len != (size_t) elt->len) {
            goto next;
        }

        for (i = 0; i < len; i++) {
            if (name[i] != elt->name[i]) {
                goto next;
            }
        }

        return elt->value;

    next:

        elt = (ngx_hash_elt_t *) ngx_align_ptr(&elt->name[0] + elt->len,
                                               sizeof(void *));
        continue;
    }

    return NULL;
}


void *
ngx_hash_find_wc_head(ngx_hash_wildcard_t *hwc, u_char *name, size_t len/*当前有效字符串的长度*/)
{
    void        *value;
    ngx_uint_t   i, n, key;

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "wch:\"%*s\"", len, name);
#endif

    n = len;
    
    /**
     * len, 为当前字符串 的有效长度
     * n,   从尾部算起,  截止到上一个"." 出现的长度
     */
    while (n) {
        if (name[n - 1] == '.') {
            break;
        }

        n--;
    }

    key = 0;

    for (i = n; i < len; i++) {
        key = ngx_hash(key, name[i]);
    }

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "key:\"%ui\"", key);
#endif

    value = ngx_hash_find(&hwc->hash, key, &name[n], len - n);

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "value:\"%p\"", value);
#endif

    if (value) {

        /**
         * the 2 low bits of value have the special meaning:
         *     00(0) - value is data pointer for both "example.com" and "*.example.com";
         *
         *     01(1) - value is data pointer for "*.example.com" only;
         *
         *     10(2) - value is pointer to wildcard hash allowing both "example.com" and "*.example.com";
         *
         *     11(3) - value is pointer to wildcard hash allowing "*.example.com" only.
         *
         *
         *		                                                                                                        
         *	        Example4  	"*.example.com"          -> server_a                                                          
         *		          		"*.mirror.example.com"   -> server_e                                                          
         *		                                                                                                        
         *		                                                                                                        
         *		           hash ---->--------+       wdc ---->+---------+       wdc' ---->+---------+                   
         *		                    |        |                |         |                 |         |                   
         *		com.example.        +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
         *		com.example.mirror. | "com"  |---->| wdc|3 |  |"example"|----->|wdc'|3|   |"mirror" |----->|server_e|1 |
         *		                    +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
         *		                    |        |                |         |                 |         |                   
         *		                    +--------+                +---------+                 |         |                   
         *		                                                                          +---------+      +-----------+
         *		                                                                          |*value(.)|----->| server_a  |
         *		                                                                          +---------+      +-----------+
         *		                                                                          |         |                   
         *		                                                                          +---------+                   
         *		    Example5  	".example.com"           -> server_b                                                          
         *		          		"*.mirror.example.com"   -> server_e                                                          
         *		                                                                                                        
         *		                                                                                                        
         *		           hash ---->--------+       wdc ---->+---------+       wdc' ---->+---------+                   
         *		                    |        |                |         |                 |         |                   
         *		com.example         +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
         *		com.example.mirror. | "com"  |---->| wdc|3 |  |"example"|----->|wdc'|2|   |"mirror" |----->|server_e|1 |
         *		                    +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
         *		                    |        |                |         |                 |         |                   
         *		                    |        |                |         |                 |         |                   
         *		                    +--------+                +---------+                 +---------+      +-----------+
         *		                                                                          |*value( )|----->| server_b  |
         *		                                                                          +---------+      +-----------+
         *		                                                                          |         |                   
         *		                                                                          +---------+                   
         *
         *  ######## dot意思:
         *      dot==3: 当前wdc连接下一级wdc', 且wdc指向一个首部通配符的server name, e.g. Example4中的example
         *      dot==2: 当前wdc连接下一级wdc', 且wdc指向一个".xxxxxxx"的server name, e.g. Example5中的example
         *      dot==1: 当前wdc无  下一级wdc', 但wdc指向一个首部通配符的server name, e.g. Example5中的mirror
         *
         *
         */
        if ((uintptr_t) value & 2) {

            if (n == 0) {   //!< 递归停止条件: 到最后一个section时, e.g. "example" in "example.com"

                /* "example.com" */

                if ((uintptr_t) value & 1) {
                    return NULL;
                }

                hwc = (ngx_hash_wildcard_t *)
                                          ((uintptr_t) value & (uintptr_t) ~3);
                return hwc->value;
            }

            hwc = (ngx_hash_wildcard_t *) ((uintptr_t) value & (uintptr_t) ~3);

            value = ngx_hash_find_wc_head(hwc, name, n - 1);

            if (value) {
                return value;
            }

            return hwc->value;
        }

        if ((uintptr_t) value & 1) {

            if (n == 0) {

                /* "example.com" */

                return NULL;
            }

            return (void *) ((uintptr_t) value & (uintptr_t) ~3);
        }

        return value;
    }

    return hwc->value;          //!< 如果找不到下一级wdc', 则返回自身的value 
}


void *
ngx_hash_find_wc_tail(ngx_hash_wildcard_t *hwc, u_char *name, size_t len)
{
    void        *value;
    ngx_uint_t   i, key;

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "wct:\"%*s\"", len, name);
#endif

    key = 0;

    for (i = 0; i < len; i++) {
        if (name[i] == '.') {
            break;
        }

        key = ngx_hash(key, name[i]);
    }

    if (i == len) {
        return NULL;
    }

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "key:\"%ui\"", key);
#endif

    value = ngx_hash_find(&hwc->hash, key, name, i);

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "value:\"%p\"", value);
#endif

    if (value) {

        /*
         * the 2 low bits of value have the special meaning:
         *     00 - value is data pointer;
         *     11 - value is pointer to wildcard hash allowing "example.*".
         */

        if ((uintptr_t) value & 2) {

            i++;

            hwc = (ngx_hash_wildcard_t *) ((uintptr_t) value & (uintptr_t) ~3);

            value = ngx_hash_find_wc_tail(hwc, &name[i], len - i);

            if (value) {
                return value;
            }

            return hwc->value;
        }

        return value;
    }

    return hwc->value;
}


void *
ngx_hash_find_combined(ngx_hash_combined_t *hash, ngx_uint_t key, u_char *name,
    size_t len)
{
    void  *value;   //!< 返回类型为 ngx_http_core_loc_conf_t *

    if (hash->hash.buckets) {   //!< 1. 先进行 安全匹配: server_name www.baidu.com
        value = ngx_hash_find(&hash->hash, key, name, len);

        if (value) {
            return value;
        }
    }

    if (len == 0) {
        return NULL;
    }

    if (hash->wc_head && hash->wc_head->hash.buckets) { //!< 2. 前缀通配符: server_name *.baidu.com
        value = ngx_hash_find_wc_head(hash->wc_head, name, len);

        if (value) {
            return value;
        }
    }

    if (hash->wc_tail && hash->wc_tail->hash.buckets) { //!< 3. 后缀通配符: server_name www.baidu.com.*
        value = ngx_hash_find_wc_tail(hash->wc_tail, name, len);

        if (value) {
            return value;
        }
    }

    return NULL;
}

//!< 计算哈希表元素 ngx_hash_elt_t大小，name为ngx_hash_elt_t结构指针
#define NGX_HASH_ELT_SIZE(name/*name.key 为ngx_hash_key_t->key.len, 计算字符串长度在 ngx_hash_elt_t所占内存大小*/) \
    (sizeof(void *) + ngx_align((name)->key.len + 2/*sizeof(u_short)*/, sizeof(void *)))    /* 对应于 ngx_hash_elt_t */

/**
 * 初始化不带通配符的哈希表, 被调用场景 ngx_http_init_headers_in_hash (初始化 http请求头, e.g. ngx_http_headers_in)
 *
 * 特点:
 *     根据实际的<key,value>键值对来实际计算每个桶的大小，而不是所有桶的大小的设置成一样的，这样能很有效的节约内存空间, 避免O(n)最坏的查找效率
 *     当然由于每个桶的大小是不固定的，所有每个桶的末尾需要一个额外空间（大小为sizeof(void*)）来标记桶的结束
 *     并且每个桶大小满足cache行对齐，这样能加快访问速度，从这里也可以看出nginx无处不在优化程序的性能和资源的使用效率
 *
 * 参考:
 *     Nginx 源代码笔记 - 哈希表 [1]   http://ialloc.org/posts/2014/06/06/ngx-notes-hashtable-1/
 *     Nginx 源代码笔记 - 哈希表 [2]   http://ialloc.org/posts/2014/06/06/ngx-notes-hashtable-2/
 */
ngx_int_t ngx_hash_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names, ngx_uint_t nelts)
{
    u_char          *elts;
    size_t           len;
    u_short         *test;
    ngx_uint_t       i, n, key, size, start, bucket_size;
    ngx_hash_elt_t  *elt, **buckets;

    for (n = 0; n < nelts; n++) {   //!< 1. volume check, 哈希桶至少能装下一个element
        if (hinit->bucket_size/*每个bucket的空间大小, 单位:字节*/ < NGX_HASH_ELT_SIZE(&names[n]) + sizeof(void *)/*void * 为结束标识符*/)
        {
            ngx_log_error(NGX_LOG_EMERG, hinit->pool->log, 0,
                          "could not build the %s, you should "
                          "increase %s_bucket_size: %i",
                          hinit->name, hinit->name, hinit->bucket_size);
            return NGX_ERROR;
        }
    }

    test = ngx_alloc(hinit->max_size/*最大桶数*/ * sizeof(u_short), hinit->pool->log);  //!< test用于记录每个桶 存储所有eleme时 用的内存大小
    if (test == NULL) {
        return NGX_ERROR;
    }

    bucket_size = hinit->bucket_size - sizeof(void *);                      //!< 每个桶槽位去掉一个结束标识符, 作为 实际桶容量(内存大小)

    /** `nelts` 个节点最少需要使用 `start` 个 bucket 才能存得下
     * 2*sizeof(void *) 为elem最小大小
     * bucket_size/(2*sizeof(void*)) 为一个桶 所存储elem 的最大个数
     * nelts/(bucket_size/(2*sizeof)) 为nelts个元素平均分到每个桶时 所需的最小桶数量
     */
    start = nelts / (bucket_size / (2 * sizeof(void *))/*ngx_hash_elt_t最小大小*/);    //!< 最小桶数, nelts元素均分到所有桶中, 得到的最小桶个数
    start = start ? start : 1;

    if (hinit->max_size > 10000 && nelts && hinit->max_size / nelts < 100) {
        start = hinit->max_size - 1000;
    }

    /** 每个桶的容量均分, 避免O(n)最坏查找效果
     * 从最小桶的个数开始递增，直到所有的<key,value>键值对都能存放在对应的桶中不溢出(不超过桶容量上限bucket_size)，那当前的桶个数就是需要的桶个数
     */
    for (size = start; size < hinit->max_size/*桶最大个数*/; size++) {

        ngx_memzero(test, size * sizeof(u_short));

        for (n = 0; n < nelts; n++) {
            if (names[n].key.data == NULL) {
                continue;
            }

            key = names[n].key_hash % size;                                     //!< 计算当前<key,value>在哪个桶
            test[key] = (u_short) (test[key] + NGX_HASH_ELT_SIZE(&names[n]));   //!< 计算当前<key,value>插入到桶之后桶的大小

#if 0
            ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0,
                          "%ui: %ui %ui \"%V\"",
                          size, key, test[key], &names[n].key);
#endif

            if (test[key] > (u_short) bucket_size) {                            //!< 检查桶是否溢出了
                goto next;
            }
        }

        goto found;

    next:

        continue;
    }

    ngx_log_error(NGX_LOG_EMERG, hinit->pool->log, 0,
                  "could not build the %s, you should increase "
                  "either %s_max_size: %i or %s_bucket_size: %i",
                  hinit->name, hinit->name, hinit->max_size,
                  hinit->name, hinit->bucket_size);

    ngx_free(test);

    return NGX_ERROR;

found:
    /**
     * 确定了 bucket桶数量后, 计算新创建的hash所占用的空间，并调用内存分配函数分配这些空间
     */

    for (i = 0; i < size/*最终确认的桶个数*/; i++) {
        test[i] = sizeof(void *);   //!< 结束符
    }

    /** !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
     * 根据实际的<key,value>键值对来实际计算每个桶的大小，而不是所有桶的大小的设置成一样的，这样能很有效的节约内存空间, 避免O(n)最坏查找效果
     * 当然由于每个桶的大小是不固定的，所有每个桶的末尾需要一个额外空间（大小为sizeof(void*)）来标记桶的结束
     * 并且每个桶大小满足cache行对齐，这样能加快访问速度，从这里也可以看出nginx无处不在优化程序的性能和资源的使用效率
     */

    //!< 计算每个桶(槽位)的实际大小
    for (n = 0; n < nelts; n++) {
        if (names[n].key.data == NULL) {
            continue;
        }

        key = names[n].key_hash % size;
        test[key] = (u_short) (test[key] + NGX_HASH_ELT_SIZE(&names[n]));       //!< 每个桶存储elem 占用的内存大小
    }

    len = 0;
    //!< 计算所有桶的 总内存大小
    for (i = 0; i < size; i++) {
        if (test[i] == sizeof(void *)) {
            continue;
        }

        test[i] = (u_short) (ngx_align(test[i], ngx_cacheline_size));           //!< 每个桶大小满足cache行对齐

        len += test[i];                                                         //!< 累加所有elem 占用的总内存大小
    }

    if (hinit->hash == NULL) {  //!< hash为NULL，则动态生成管理hash的结构 for 通配符哈希表
        hinit->hash = ngx_pcalloc(hinit->pool, sizeof(ngx_hash_wildcard_t) + size * sizeof(ngx_hash_elt_t *));//!< calloc会把获取的内存初始化为0
        if (hinit->hash == NULL) {
            ngx_free(test);
            return NGX_ERROR;
        }

        buckets = (ngx_hash_elt_t **)((u_char *) hinit->hash + sizeof(ngx_hash_wildcard_t));
    } else {
        buckets = ngx_pcalloc(hinit->pool, size/*实际桶个数*/ * sizeof(ngx_hash_elt_t *));    //!< 分配实际哈希桶(槽位), 分配 每个桶, 桶记录下起始地址即可
        if (buckets == NULL) {
            ngx_free(test);
            return NGX_ERROR;
        }
    }

    elts = ngx_palloc(hinit->pool, len + ngx_cacheline_size);                   //!< 将所有桶占用的空间分配在连续的内存空间中, 起始地址存于elts
    if (elts == NULL) {
        ngx_free(test);
        return NGX_ERROR;
    }

    elts = ngx_align_ptr(elts, ngx_cacheline_size);                             //!< elts起始地址 cacheline对齐

    for (i = 0; i < size; i++) {
        if (test[i] == sizeof(void *)) {
            continue;
        }

        buckets[i] = (ngx_hash_elt_t *) elts;                                   //!< 确定每个桶的首地址
        elts += test[i];

    }

    for (i = 0; i < size; i++) {
        test[i] = 0;                                                            //!< 每个桶 现有长度
    }

    /** 将每个<key,value>键值对复制到所在的桶中
     * 根据输入的ngx_hash_key_t *names, 生成最终的哈希表
     */
    for (n = 0; n < nelts; n++) {
        if (names[n].key.data == NULL) {
            continue;
        }

        key = names[n].key_hash % size;
        elt = (ngx_hash_elt_t *) ((u_char *) buckets[key] + test[key]);         //!< name[n] 对应的 element地址

        elt->value = names[n].value;                                            //!< value地址
        elt->len = (u_short) names[n].key.len;

        ngx_strlow(elt->name, names[n].key.data, names[n].key.len);

        test[key] = (u_short) (test[key] + NGX_HASH_ELT_SIZE(&names[n]));       //!< 更新当前桶的现有长度
    }

    for (i = 0; i < size; i++) {                                                //!< 迭代每个桶, 表示每个桶的结束位置
        if (buckets[i] == NULL) {
            continue;
        }

        elt = (ngx_hash_elt_t *) ((u_char *) buckets[i] + test[i]);

        elt->value = NULL;                                                      //!< 标示 此bucket[i] 结束符
    }

    ngx_free(test);

    hinit->hash->buckets = buckets;         //!< 哈希表的 桶的首地址
    hinit->hash->size = size;               //!< 哈希表的 桶的个数

    return NGX_OK;
}

/** 构建带有通配符的 "多级哈希表链表"
 *
 * convert  "*.example.com" to "com.example.\0"     -- 前置通配符
 *      and ".example.com"  to "com.example\0"      -- 前置正则
 *      and "example.com.*" to "example.com\0"      -- 后置通配符
 * 将转换后的字符串 保存在ngx_hash_keys_arrays_t ha.[dns_wc_head | dns_wc_tail]中
 *  
 * 参考文档: 
 *  1. Nginx 源代码笔记 - 哈希表 [2], http://ialloc.org/posts/2014/06/06/ngx-notes-hashtable-2/
 *  2. nginx源码分析之hash的实现, http://www.cnblogs.com/chengxuyuancc/p/3782808.html
 *
 *
 * 关于wildcard hash构造,图解如下:
 *		(1). Example1. "*.example.com" -> server_a                                                                   
 *		                                                                                                        
 *		                                                                                                        
 *		          hash ---->--------+            wdc ---->+---------+                                           
 *		                   |        |                     |         |                                           
 *		                   +--------+       +-------+     +---------+      +-----------+                        
 *		 com.example.      | "com"  |------>| wdc|3 |     |"example"|----->|server_a|1 |                        
 *		                   +--------+       +-------+     +---------+      +-----------+                        
 *		                   |        |                     |         |                                           
 *		                   +--------+                     +---------+                                           
 *		                                                                                                        
 *		                                                                                                        
 *		                                                                                                        
 *		                                                                                                        
 *		(2). Example2. ".example.com" -> server_b                                                                    
 *		                                                                                                        
 *		           hash ---->--------+            wdc ---->+---------+                                          
 *		                    |        |                     |         |                                          
 *		                    +--------+       +-------+     +---------+      +---------+                         
 *		  com.example       | "com"  |------>| wdc|3 |     |"example"|----->|server_b |                         
 *		                    +--------+       +-------+     +---------+      +---------+                         
 *		                    |        |                     |         |                                          
 *		                    +--------+                     +---------+                                          
 *		                                                                                                        

 *		(3). Example3  	".example.com" -> server_b                                                                    
 *		          		"*.sample.com  -> server_c                                                                    
 *		                                                                                                        
 *		           hash ---->--------+            wdc ---->+---------+                                          
 *		                    |        |                     |         |                                          
 *		   com.example      +--------+      +-------+      +---------+      +---------+                         
 *		   com.sample.      | "com"  |------> wdc|3 |      |"example"|----->|server_b |                         
 *		                    +--------+      +-------+      +---------+      +---------+                         
 *		                    |        |                     |         |                                          
 *		                    +--------+                     +---------+      +-----------+                       
 *		                                                   |"sample" |----->|server_c|1 |                       
 *		                                                   +---------+      +-----------+                       
 *		                                                   |         |                                          
 *		                                                   +---------+                                          
 *		                                                                                                        
 *		                                                                                                        
 *		(4). Example4  	"*.example.com"          -> server_a                                                          
 *		          		"*.mirror.example.com"   -> server_e                                                          
 *		                                                                                                        
 *		                                                                                                        
 *		           hash ---->--------+       wdc ---->+---------+       wdc' ---->+---------+                   
 *		                    |        |                |         |                 |         |                   
 *		com.example.        +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
 *		com.example.mirror. | "com"  |---->| wdc|3 |  |"example"|----->|wdc'|3|   |"mirror" |----->|server_e|1 |
 *		                    +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
 *		                    |        |                |         |                 |         |                   
 *		                    +--------+                +---------+                 |         |                   
 *		                                                                          +---------+      +-----------+
 *		                                                                          |*value(.)|----->| server_a  |
 *		                                                                          +---------+      +-----------+
 *		                                                                          |         |                   
 *		                                                                          +---------+                   
 *		(5). Example5  	".example.com"           -> server_b                                                          
 *		          		"*.mirror.example.com"   -> server_e                                                          
 *		                                                                                                        
 *		                                                                                                        
 *		           hash ---->--------+       wdc ---->+---------+       wdc' ---->+---------+                   
 *		                    |        |                |         |                 |         |                   
 *		com.example         +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
 *		com.example.mirror. | "com"  |---->| wdc|3 |  |"example"|----->|wdc'|2|   |"mirror" |----->|server_e|1 |
 *		                    +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
 *		                    |        |                |         |                 |         |                   
 *		                    |        |                |         |                 |         |                   
 *		                    +--------+                +---------+                 +---------+      +-----------+
 *		                                                                          |*value( )|----->| server_b  |
 *		                                                                          +---------+      +-----------+
 *		                                                                          |         |                   
 *		                                                                          +---------+                   
 *
 *  ######## dot意思:
 *      dot==3: 当前wdc连接下一级wdc', 且wdc指向一个首部通配符的server name, e.g. Example4中的example
 *      dot==2: 当前wdc连接下一级wdc', 且wdc指向一个".xxxxxxx"的server name, e.g. Example5中的example
 *      dot==1: 当前wdc无  下一级wdc', 但wdc指向一个首部通配符的server name, e.g. Example5中的mirror
 *
 */
ngx_int_t ngx_hash_wildcard_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
        ngx_uint_t nelts)
{
    size_t                len, dot_len;
    ngx_uint_t            i, n, dot;
    ngx_array_t           curr_names, next_names;
    ngx_hash_key_t       *name, *next_name;
    ngx_hash_init_t       h;
    ngx_hash_wildcard_t  *wdc;

    if (ngx_array_init(&curr_names, hinit->temp_pool, nelts, sizeof(ngx_hash_key_t)) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_array_init(&next_names, hinit->temp_pool, nelts, sizeof(ngx_hash_key_t)) != NGX_OK) {
        return NGX_ERROR;
    }

    /**
     * 1. com.baidu.test. split(".") 分成分成3个section单元["com", "baidu", "test"]
     * 2. 判断后面紧邻的主机名 (所有主机名已经排序，拥有相同单元的主机名依次紧邻) 是 否和当前主机名的第一个单元相同
     * 3. 如果有，将这些主机名剩余部分都存到 next_names 中，同时，主循环中不会再次处理这些主机名 (n = i)。
     */
    for (n = 0; n < nelts; n = i) {
#if 0
        ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0, "wc0: \"%V\"", &names[n].key);
#endif

        dot = 0;

        for (len = 0; len < names[n].key.len; len++) {  //!< 1.找到section1: 对于第n个key, 找到第一个为"."的位置, len为从开始到"."的长度
            if (names[n].key.data[len] == '.') {
                dot = 1;
                break;
            }
        }

        name = ngx_array_push(&curr_names);
        if (name == NULL) {
            return NGX_ERROR;
        }

        name->key.len = len;
        name->key.data = names[n].key.data;
        name->key_hash = hinit->key(name->key.data, name->key.len);
        name->value = names[n].value;

#if 0
        ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0, "wc1: \"%V\" %ui", &name->key, dot);
#endif

        dot_len = len + 1;  //!< 加上"."的段长度

        if (dot) {
            len++;
        }

        next_names.nelts = 0;

        if (names[n].key.len != len) {                  //!< 2. 找到section2-sectionN: "."不位于字符串末尾, 第一个"."之后的所有的字符串
            next_name = ngx_array_push(&next_names);
            if (next_name == NULL) {
                return NGX_ERROR;
            }

            next_name->key.len = names[n].key.len - len;
            next_name->key.data = names[n].key.data + len;
            next_name->key_hash = 0;
            next_name->value = names[n].value;

#if 0
            ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0, "wc2: \"%V\"", &next_name->key);
#endif
        }

        /** 
         * 1. 场景: 以前缀通配符为例, 说明此处代码作用, 后缀通配符场景类似
         * 
         * 2. 功能:
         *      判断后面紧邻的主机名 (所有主机名已经排序，拥有相同单元的主机名依次紧邻) 是 否和当前主机名的第一个单元相同。
         *      如果有，将这些主机名剩余部分都存到 next_names 中，同时，主循环中不会再次处理这些主机名 (n = i)。
         * 
         * 3. server_name 原始输入如下: 
         *      *.baidu.com         <--->   com.baidu.\0
         *      .news.baidu.com     <--->   com.baidu.news\0
         *      .sina.com.cn        <--->   cn.com.sina\0
         *      .nba.com.cn         <--->   cn.com.nba\0
         * 
         * 4. ngx_http_cmp_dns_wildcards 排序后如下:
         *      (1) cn.com.sina\0
         *      (2) cn.com.nba\0
         *      (3) com.baidu.\0
         *      (4) com.baidu.news\0
         */
        for (i = n + 1; i < nelts; i++/*(2)-(4)中有几个和(1)的第一个section 字符串相同*/) {
            /**
             * (1) cn 和 (2) cn 相同, 则往下走, 以记录下相同cn之后的 所有剩余字符串, 并发剩余字符串记录在next_name中
             * (2) cn 和 (3) com不同, 则直接跳出for
             */
            if (ngx_strncmp(names[n].key.data, names[i].key.data, len/*当前names[n].key.data 遇到.之前的长度*/) != 0) {
                break;
            }

            /** 情形
             * com.baidu\0                          ===> baidu\0                    ===> ngx_hash_wildcard_init("baidu\0")
             * com.baiduzol.\0 or com.baiduzol\0    ===> baiduzol.\0 or baiduzol\0  ===> ngx_hash_wildcard_init("baiduzol.\0")
             */
            if (!dot && names[i].key.len > len && names[i].key.data[len] != '.') {
                break;  //!< baidu\0 和 baiduzol.\0 很明显不同, 所以break分开处理
            }

            next_name = ngx_array_push(&next_names);
            if (next_name == NULL) {
                return NGX_ERROR;
            }

            /**
             * 对于4.(1)和4.(2), next_names 中 存有 com.sina\0, com.nba\0
             */
            next_name->key.len = names[i].key.len - dot_len;
            next_name->key.data = names[i].key.data + dot_len;  //!< 相同部分 后面的所有字符串
            next_name->key_hash = 0;
            next_name->value = names[i].value;

#if 0
            ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0, "wc3: \"%V\"", &next_name->key);
#endif
        }

        /**
         * 只有最后一段字符 next_names.nelts == 0, 这是递归的停止条件
         */
        if (next_names.nelts) {

            h = *hinit;
            h.hash = NULL;

            if (ngx_hash_wildcard_init(&h, (ngx_hash_key_t *) next_names.elts, next_names.nelts) != NGX_OK) {
                return NGX_ERROR;
            }

            /** 每次通过ngx_hash_wildcard_init动态生成的h.hash, 其地址记录在name-value上
             * 为next_names动态创建完哈希表后, 用wdc记下 动态生成的地址
             * 并且 当前字段( name = ngx_array_push(&curr_names);  )的value = 此地址 | (低两位控制标记)
             */
            wdc/*ngx_hash_wildcard_t *wdc*/ = (ngx_hash_wildcard_t *) h.hash;

            /**
             * 1. 用途
             *      当使用这个ngx_hash_wildcard_t通配符散列表 存储相应的value时(ngx_http_core_srv_conf_t)，可以使用这个value指针指向用户数据
             * 2. 例子一
             *                	"*.example.com"          -> server_a                                                          
             *		     		"*.mirror.example.com"   -> server_e                                                          
             *		                                                                                                        
             *		           hash ---->--------+       wdc ---->+---------+       wdc' ---->+---------+                   
             *		                    |        |                |         |                 |         |                   
             *		com.example.        +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
             *		com.example.mirror. | "com"  |---->| wdc|3 |  |"example"|----->|wdc'|3|   |"mirror" |----->|server_e|1 |
             *		                    +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
             *		                    |        |                |         |                 |         |                   
             *		                    +--------+                +---------+                 |         |                   
             *		                                                                          +---------+      +-----------+
             *		                                                                          |*value(.)|----->| server_a  |
             *		                                                                          +---------+      +-----------+
             *		                                                                          |         |                   
             *		                                                                          +---------+    
             * example对应的哈希表项 既有本身value(com.example. 对应ngx_http_core_srv_conf_t), 又包括 下一级哈希表项(com.example.mirror.)
             * 故, 需要用value 存储 com.example. 对应ngx_http_core_srv_conf_t地址
             */
            if (names[n].key.len == len) {
                wdc->value = names[n].value;
            }

            /** 将后缀组成的下一级hash地址作为当前字段的value保存下来
             * 若未到最后一级时, name->value 指向 下一级哈希表结构的地址 wdc
             *
             * dot = 2, examle指向下一级哈希表, 但包括一个com.example情形的情况, 所有用2标识
             *	 例子二	    ".example.com"           -> server_b                                                          
			 *              "*.mirror.example.com"   -> server_e                                                          
			 *                                                                                                         
			 *            hash ---->--------+       wdc ---->+---------+       wdc' ---->+---------+                   
			 *                     |        |                |         |                 |         |                   
			 * com.example         +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
			 * com.example.mirror. | "com"  |---->| wdc|3 |  |"example"|----->|wdc'|2|   |"mirror" |----->|server_e|1 |
			 *                     +--------+     +-------+  +---------+      +------+   +---------+      +-----------+
			 *                     |        |                |         |                 |         |                   
			 *                     |        |                |         |                 |         |                   
			 *                     +--------+                +---------+                 +---------+      +-----------+
			 *                                                                           |*value( )|----->| server_b  |
			 *                                                                           +---------+      +-----------+
			 *                                                                           |         |                   
             *                                                                           +---------+                    
             *
             *
             *  ######## dot意思:
             *      dot==3: 当前wdc连接下一级wdc', 且wdc指向一个首部通配符的server name, e.g. 例子一中的example
             *      dot==2: 当前wdc连接下一级wdc', 且wdc指向一个".xxxxxxx"的server name, e.g. 例子二中的example
             *      dot==1: 当前wdc无  下一级wdc', 但wdc指向一个首部通配符的server name, e.g. 例子二中的mirror
             */
            name->value = (void *) ((uintptr_t) wdc | (dot ? 3 : 2));   //!< 当前哈希表的value 指向下一级 的哈希表地址

        } else if (dot) {   //!< 匹配最后一个字段时(example.), 存在'.' 但不存在next_names
            name->value = (void *) ((uintptr_t) name->value | 1);   //!< 匹配场景: *.example.com <--> com.example.\0
        }
    }   //!< for (n = 0; n < nelts; n = i)

    /**
     * 怎样标记一个键值对<key,value>中的value是指向实际的value，还是指向下一级的hash地址，这是上面代码实现的一个巧妙的地方
     * 由于每个hash表的地址或者实际value的地址都是以4字节对齐的，所以这些地址的低2位都是0，这样通过这两位的标记可以很好地解决这个问题
     */

    /**
     * 递归建立 各级hash表
     * 根据<当前字段,value>键值对建立hash
     */
    if (ngx_hash_init(hinit, (ngx_hash_key_t *) curr_names.elts, curr_names.nelts) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_uint_t
ngx_hash_key(u_char *data, size_t len)
{
    ngx_uint_t  i, key;

    key = 0;

    for (i = 0; i < len; i++) {
        key = ngx_hash(key, data[i]);
    }

    return key;
}


ngx_uint_t
ngx_hash_key_lc(u_char *data, size_t len)
{
    ngx_uint_t  i, key;

    key = 0;

    for (i = 0; i < len; i++) {
        key = ngx_hash(key, ngx_tolower(data[i]));
    }

    return key;
}

//!< 先转换成小写, 然后计算哈希值
ngx_uint_t
ngx_hash_strlow(u_char *dst, u_char *src, size_t n)
{
    ngx_uint_t  key;

    key = 0;

    while (n--) {
        *dst = ngx_tolower(*src);
        key = ngx_hash(key, *dst);
        dst++;
        src++;
    }

    return key;
}


ngx_int_t
ngx_hash_keys_array_init(ngx_hash_keys_arrays_t *ha, ngx_uint_t type)
{
    ngx_uint_t  asize;

    if (type == NGX_HASH_SMALL) {
        asize = 4;
        ha->hsize = 107;

    } else {
        asize = NGX_HASH_LARGE_ASIZE;           //!< value 内存池 预分配个数
        ha->hsize = NGX_HASH_LARGE_HSIZE;       //!< key 哈希桶数
    }

    /**
     * key存储      --- server_name对应的 ngx_str_t
     * value存储    --- ngx_hash_key_t
     */

    if (ngx_array_init(&ha->keys, ha->temp_pool, asize, sizeof(ngx_hash_key_t))         //!< 完全匹配 value存储数组 
        != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_array_init(&ha->dns_wc_head, ha->temp_pool, asize, sizeof(ngx_hash_key_t))  //!< 前缀通配符 value存储数组
        != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_array_init(&ha->dns_wc_tail, ha->temp_pool, asize, sizeof(ngx_hash_key_t))  //!< 后缀通配符 value存储数组
        != NGX_OK) {
        return NGX_ERROR;
    }

    ha->keys_hash = ngx_pcalloc(ha->temp_pool, sizeof(ngx_array_t) * ha->hsize);        //!< 完全匹配 key哈希表, 桶数量为ha->hsize
    if (ha->keys_hash == NULL) {
        return NGX_ERROR;
    }

    ha->dns_wc_head_hash = ngx_pcalloc(ha->temp_pool, sizeof(ngx_array_t) * ha->hsize); //!< 前缀通配符 key哈希表, 桶数量为ha->hsize
    if (ha->dns_wc_head_hash == NULL) {
        return NGX_ERROR;
    }

    ha->dns_wc_tail_hash = ngx_pcalloc(ha->temp_pool, sizeof(ngx_array_t) * ha->hsize); //!< 后缀通配符 key哈希表, 桶数量为ha->hsize
    if (ha->dns_wc_tail_hash == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_hash_add_key(ngx_hash_keys_arrays_t *ha, ngx_str_t *key, void *value,
    ngx_uint_t flags/*标记 是否包括通配符*/)
{
    size_t           len;
    u_char          *p;
    ngx_str_t       *name;
    ngx_uint_t       i, k, n, skip, last;
    ngx_array_t     *keys, *hwc;
    ngx_hash_key_t  *hk;
                        //!< skip为去掉通配符后的 第一个有效字符的位置, 从0开始
    last = key->len;    //!< last为最后一个有效字符 对应的有效字符串长度, 从1开始

    /**
     * 对于带有 通配符场景, 下面这个if 做sanity check
     */
    if (flags & NGX_HASH_WILDCARD_KEY) {

        /*
         * supported wildcards:
         *     "*.example.com", ".example.com", and "www.example.*"
         */

        n = 0;

        /**
         * sanity check: 1)*使用限制; 2)只能存在一个.
         */
        for (i = 0; i < key->len; i++) {

            /** *使用限制: 1)通配符只能在开头or结尾; 2)必须与.配套: *.xxx or xxx.*
             * A wildcard name may contain an asterisk only on the name's start or end, and only on a dot border.
             * An asterisk can match several name parts.
             */
            if (key->data[i] == '*') {
                if (++n > 1) {
                    return NGX_DECLINED;
                }
            }

            /**
             * 只能存在一个.
             */
            if (key->data[i] == '.' && key->data[i + 1] == '.') {
                return NGX_DECLINED;
            }
        }

        /**
         * .example.com, skip==1
         */
        if (key->len > 1 && key->data[0] == '.') {
            skip = 1;           //!< skip为第一个有效字符的位置
            goto wildcard;      //!< 通配符处理
        }

        if (key->len > 2) {

            /**
             * *.example.com, skip==2
             */
            if (key->data[0] == '*' && key->data[1] == '.') {
                skip = 2;       //!< skip为第一个有效字符的位置
                goto wildcard;  //!< 通配符处理
            }

            /**
             * example.com.*, skip==-2
             */
            if (key->data[i - 2] == '.' && key->data[i - 1] == '*') {
                skip = 0;       //!< skip为第一个有效字符的位置
                last -= 2;      //!< last为最后一个有效字符 对应的有效字符串长度
                goto wildcard;  //!< 通配符处理
            }
        }

        if (n) {    //!< 若不是 *.xxx or xxx.* 通配符场景(通配符出现在中间位置), 则视为无效场景
            return NGX_DECLINED;
        }
    }

    /* exact hash */
    /* 无通配符 hash */
    k = 0;

    for (i = 0; i < last/*最后一个有效字符的位置*/; i++) {
        if (!(flags & NGX_HASH_READONLY_KEY)) {
            key->data[i] = ngx_tolower(key->data[i]);   //!< 转成小写
        }
        k = ngx_hash(k, key->data[i]);                  //!< 计算字符串的 hash值
    }

    k %= ha->hsize; //!< key对应的 hash buckets 槽位

    /* check conflicts in exact hash */
    //!< 检测是否有冲突
    name = ha->keys_hash[k].elts;

    if (name) {
        for (i = 0; i < ha->keys_hash[k].nelts; i++) {
            if (last != name[i].len) {
                continue;
            }

            if (ngx_strncmp(key->data, name[i].data, last) == 0) {
                return NGX_BUSY;
            }
        }

    } else {
        if (ngx_array_init(&ha->keys_hash[k], ha->temp_pool, 4,
                           sizeof(ngx_str_t))
            != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    name = ngx_array_push(&ha->keys_hash[k]);
    if (name == NULL) {
        return NGX_ERROR;
    }

    *name = *key;   //!< key存储 --- ngx_str_t 赋值拷贝

    /**
     * value 存储 --- ngx_hash_key_t
     */
    hk = ngx_array_push(&ha->keys);
    if (hk == NULL) {
        return NGX_ERROR;
    }

    /**
     * 初始化ngx_hash_key_t, 以填充不包括通配符的哈希表
     */
    hk->key = *key;                                 //< ngx_str_t
    hk->key_hash = ngx_hash_key(key->data, last);   //!< hash value
    hk->value = value;                              //!< value 地址赋值

    return NGX_OK;


wildcard:

    /* wildcard hash */

    k = ngx_hash_strlow(&key->data[skip], &key->data[skip], last - skip);   //!< 去除通配符, 计算hash值

    k %= ha->hsize;     //!< hash bucket num

    if (skip == 1) {    //!< .example.com, skip==1

        /* check conflicts in exact hash for ".example.com" */
        //!< 检测 key 是否有冲突
        name = ha->keys_hash[k].elts;

        if (name) {
            len = last - skip;

            for (i = 0; i < ha->keys_hash[k].nelts; i++) {
                if (len != name[i].len) {
                    continue;
                }

                if (ngx_strncmp(&key->data[1], name[i].data, len) == 0) {
                    return NGX_BUSY;
                }
            }

        } else {
            if (ngx_array_init(&ha->keys_hash[k], ha->temp_pool, 4,
                               sizeof(ngx_str_t))
                != NGX_OK)
            {
                return NGX_ERROR;
            }
        }

        name = ngx_array_push(&ha->keys_hash[k]);           //!< key 存储
        if (name == NULL) {
            return NGX_ERROR;
        }

        name->len = last - 1;
        name->data = ngx_pnalloc(ha->temp_pool, name->len);
        if (name->data == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(name->data, &key->data[1], name->len);   //!< 省略.
    }


    if (skip) { //!< *.example.com(skip==2) or .example.com(skip==1)

        p = ngx_pnalloc(ha->temp_pool, last);
        if (p == NULL) {
            return NGX_ERROR;
        }

        len = 0;
        n = 0;

        /**
         * convert "*.example.com" to "com.example\0"
         *      and ".example.com" to "com.example\0"
         * 将转换后的字符串 保存在p[]中
         */
        for (i = last - 1; i; i--) {
            if (key->data[i] == '.') {
                ngx_memcpy(&p[n], &key->data[i + 1], len);
                n += len;
                p[n++] = '.';
                len = 0;
                continue;
            }

            len++;
        }

        if (len) {  //!< *.example.com场景, for循环后 len==1
            ngx_memcpy(&p[n], &key->data[1], len);  //!< 加上.
            n += len;
        }

        p[n] = '\0';    //!< 结束符

        hwc = &ha->dns_wc_head;             //!< wildcard header value存储
        keys = &ha->dns_wc_head_hash[k];    //!< wildcard header key存储

    } else {    //!< example.com.*(后置通配符), skip==-2

        /* convert "www.example.*" to "www.example\0" */

        last++; //!< 之前, last = key->len - 2; 此时++,为了增加一个\0位置

        p = ngx_pnalloc(ha->temp_pool, last);
        if (p == NULL) {
            return NGX_ERROR;
        }

        ngx_cpystrn(p, key->data, last);    //!< ngx_cpystrn会设置p的结束符'\0'

        hwc = &ha->dns_wc_tail;             //!< wildcard tail value存储
        keys = &ha->dns_wc_tail_hash[k];    //!< wildcard tail key存储
    }

    hk = ngx_array_push(hwc);               //!< wildcard header/tail value存储
    if (hk == NULL) {
        return NGX_ERROR;
    }

    hk->key.len = last - 1;                 //!< 去除'\0'
    hk->key.data = p;                       //!< 将 处理后的通配符, 存入
    hk->key_hash = 0;
    hk->value = value;                      //!< value存储的是地址

    /* check conflicts in wildcard hash */
    //!< wildcard 在key中 检测是否有冲突
    name = keys->elts;

    if (name) {
        len = last - skip;

        for (i = 0; i < keys->nelts; i++) {
            if (len != name[i].len) {
                continue;
            }

            if (ngx_strncmp(key->data + skip, name[i].data, len) == 0) {
                return NGX_BUSY;
            }
        }

    } else {
        if (ngx_array_init(keys, ha->temp_pool, 4, sizeof(ngx_str_t)) != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    name = ngx_array_push(keys);        //!< wildcard header/tail key存储
    if (name == NULL) {
        return NGX_ERROR;
    }

    name->len = last - skip;
    name->data = ngx_pnalloc(ha->temp_pool, name->len);
    if (name->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(name->data, key->data + skip, name->len);

    return NGX_OK;
}
