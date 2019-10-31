
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
ngx_hash_find_wc_head(ngx_hash_wildcard_t *hwc, u_char *name, size_t len/*��ǰ��Ч�ַ����ĳ���*/)
{
    void        *value;
    ngx_uint_t   i, n, key;

#if 0
    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0, "wch:\"%*s\"", len, name);
#endif

    n = len;
    
    /**
     * len, Ϊ��ǰ�ַ��� ����Ч����
     * n,   ��β������,  ��ֹ����һ��"." ���ֵĳ���
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
         *  ######## dot��˼:
         *      dot==3: ��ǰwdc������һ��wdc', ��wdcָ��һ���ײ�ͨ�����server name, e.g. Example4�е�example
         *      dot==2: ��ǰwdc������һ��wdc', ��wdcָ��һ��".xxxxxxx"��server name, e.g. Example5�е�example
         *      dot==1: ��ǰwdc��  ��һ��wdc', ��wdcָ��һ���ײ�ͨ�����server name, e.g. Example5�е�mirror
         *
         *
         */
        if ((uintptr_t) value & 2) {

            if (n == 0) {   //!< �ݹ�ֹͣ����: �����һ��sectionʱ, e.g. "example" in "example.com"

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

    return hwc->value;          //!< ����Ҳ�����һ��wdc', �򷵻������value 
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
    void  *value;   //!< ��������Ϊ ngx_http_core_loc_conf_t *

    if (hash->hash.buckets) {   //!< 1. �Ƚ��� ��ȫƥ��: server_name www.baidu.com
        value = ngx_hash_find(&hash->hash, key, name, len);

        if (value) {
            return value;
        }
    }

    if (len == 0) {
        return NULL;
    }

    if (hash->wc_head && hash->wc_head->hash.buckets) { //!< 2. ǰ׺ͨ���: server_name *.baidu.com
        value = ngx_hash_find_wc_head(hash->wc_head, name, len);

        if (value) {
            return value;
        }
    }

    if (hash->wc_tail && hash->wc_tail->hash.buckets) { //!< 3. ��׺ͨ���: server_name www.baidu.com.*
        value = ngx_hash_find_wc_tail(hash->wc_tail, name, len);

        if (value) {
            return value;
        }
    }

    return NULL;
}

//!< �����ϣ��Ԫ�� ngx_hash_elt_t��С��nameΪngx_hash_elt_t�ṹָ��
#define NGX_HASH_ELT_SIZE(name/*name.key Ϊngx_hash_key_t->key.len, �����ַ��������� ngx_hash_elt_t��ռ�ڴ��С*/) \
    (sizeof(void *) + ngx_align((name)->key.len + 2/*sizeof(u_short)*/, sizeof(void *)))    /* ��Ӧ�� ngx_hash_elt_t */

/**
 * ��ʼ������ͨ����Ĺ�ϣ��, �����ó��� ngx_http_init_headers_in_hash (��ʼ�� http����ͷ, e.g. ngx_http_headers_in)
 *
 * �ص�:
 *     ����ʵ�ʵ�<key,value>��ֵ����ʵ�ʼ���ÿ��Ͱ�Ĵ�С������������Ͱ�Ĵ�С�����ó�һ���ģ������ܺ���Ч�Ľ�Լ�ڴ�ռ�, ����O(n)��Ĳ���Ч��
 *     ��Ȼ����ÿ��Ͱ�Ĵ�С�ǲ��̶��ģ�����ÿ��Ͱ��ĩβ��Ҫһ������ռ䣨��СΪsizeof(void*)�������Ͱ�Ľ���
 *     ����ÿ��Ͱ��С����cache�ж��룬�����ܼӿ�����ٶȣ�������Ҳ���Կ���nginx�޴������Ż���������ܺ���Դ��ʹ��Ч��
 *
 * �ο�:
 *     Nginx Դ����ʼ� - ��ϣ�� [1]   http://ialloc.org/posts/2014/06/06/ngx-notes-hashtable-1/
 *     Nginx Դ����ʼ� - ��ϣ�� [2]   http://ialloc.org/posts/2014/06/06/ngx-notes-hashtable-2/
 */
ngx_int_t ngx_hash_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names, ngx_uint_t nelts)
{
    u_char          *elts;
    size_t           len;
    u_short         *test;
    ngx_uint_t       i, n, key, size, start, bucket_size;
    ngx_hash_elt_t  *elt, **buckets;

    for (n = 0; n < nelts; n++) {   //!< 1. volume check, ��ϣͰ������װ��һ��element
        if (hinit->bucket_size/*ÿ��bucket�Ŀռ��С, ��λ:�ֽ�*/ < NGX_HASH_ELT_SIZE(&names[n]) + sizeof(void *)/*void * Ϊ������ʶ��*/)
        {
            ngx_log_error(NGX_LOG_EMERG, hinit->pool->log, 0,
                          "could not build the %s, you should "
                          "increase %s_bucket_size: %i",
                          hinit->name, hinit->name, hinit->bucket_size);
            return NGX_ERROR;
        }
    }

    test = ngx_alloc(hinit->max_size/*���Ͱ��*/ * sizeof(u_short), hinit->pool->log);  //!< test���ڼ�¼ÿ��Ͱ �洢����elemeʱ �õ��ڴ��С
    if (test == NULL) {
        return NGX_ERROR;
    }

    bucket_size = hinit->bucket_size - sizeof(void *);                      //!< ÿ��Ͱ��λȥ��һ��������ʶ��, ��Ϊ ʵ��Ͱ����(�ڴ��С)

    /** `nelts` ���ڵ�������Ҫʹ�� `start` �� bucket ���ܴ����
     * 2*sizeof(void *) Ϊelem��С��С
     * bucket_size/(2*sizeof(void*)) Ϊһ��Ͱ ���洢elem ��������
     * nelts/(bucket_size/(2*sizeof)) Ϊnelts��Ԫ��ƽ���ֵ�ÿ��Ͱʱ �������СͰ����
     */
    start = nelts / (bucket_size / (2 * sizeof(void *))/*ngx_hash_elt_t��С��С*/);    //!< ��СͰ��, neltsԪ�ؾ��ֵ�����Ͱ��, �õ�����СͰ����
    start = start ? start : 1;

    if (hinit->max_size > 10000 && nelts && hinit->max_size / nelts < 100) {
        start = hinit->max_size - 1000;
    }

    /** ÿ��Ͱ����������, ����O(n)�����Ч��
     * ����СͰ�ĸ�����ʼ������ֱ�����е�<key,value>��ֵ�Զ��ܴ���ڶ�Ӧ��Ͱ�в����(������Ͱ��������bucket_size)���ǵ�ǰ��Ͱ����������Ҫ��Ͱ����
     */
    for (size = start; size < hinit->max_size/*Ͱ������*/; size++) {

        ngx_memzero(test, size * sizeof(u_short));

        for (n = 0; n < nelts; n++) {
            if (names[n].key.data == NULL) {
                continue;
            }

            key = names[n].key_hash % size;                                     //!< ���㵱ǰ<key,value>���ĸ�Ͱ
            test[key] = (u_short) (test[key] + NGX_HASH_ELT_SIZE(&names[n]));   //!< ���㵱ǰ<key,value>���뵽Ͱ֮��Ͱ�Ĵ�С

#if 0
            ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0,
                          "%ui: %ui %ui \"%V\"",
                          size, key, test[key], &names[n].key);
#endif

            if (test[key] > (u_short) bucket_size) {                            //!< ���Ͱ�Ƿ������
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
     * ȷ���� bucketͰ������, �����´�����hash��ռ�õĿռ䣬�������ڴ���亯��������Щ�ռ�
     */

    for (i = 0; i < size/*����ȷ�ϵ�Ͱ����*/; i++) {
        test[i] = sizeof(void *);   //!< ������
    }

    /** !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
     * ����ʵ�ʵ�<key,value>��ֵ����ʵ�ʼ���ÿ��Ͱ�Ĵ�С������������Ͱ�Ĵ�С�����ó�һ���ģ������ܺ���Ч�Ľ�Լ�ڴ�ռ�, ����O(n)�����Ч��
     * ��Ȼ����ÿ��Ͱ�Ĵ�С�ǲ��̶��ģ�����ÿ��Ͱ��ĩβ��Ҫһ������ռ䣨��СΪsizeof(void*)�������Ͱ�Ľ���
     * ����ÿ��Ͱ��С����cache�ж��룬�����ܼӿ�����ٶȣ�������Ҳ���Կ���nginx�޴������Ż���������ܺ���Դ��ʹ��Ч��
     */

    //!< ����ÿ��Ͱ(��λ)��ʵ�ʴ�С
    for (n = 0; n < nelts; n++) {
        if (names[n].key.data == NULL) {
            continue;
        }

        key = names[n].key_hash % size;
        test[key] = (u_short) (test[key] + NGX_HASH_ELT_SIZE(&names[n]));       //!< ÿ��Ͱ�洢elem ռ�õ��ڴ��С
    }

    len = 0;
    //!< ��������Ͱ�� ���ڴ��С
    for (i = 0; i < size; i++) {
        if (test[i] == sizeof(void *)) {
            continue;
        }

        test[i] = (u_short) (ngx_align(test[i], ngx_cacheline_size));           //!< ÿ��Ͱ��С����cache�ж���

        len += test[i];                                                         //!< �ۼ�����elem ռ�õ����ڴ��С
    }

    if (hinit->hash == NULL) {  //!< hashΪNULL����̬���ɹ���hash�Ľṹ for ͨ�����ϣ��
        hinit->hash = ngx_pcalloc(hinit->pool, sizeof(ngx_hash_wildcard_t) + size * sizeof(ngx_hash_elt_t *));//!< calloc��ѻ�ȡ���ڴ��ʼ��Ϊ0
        if (hinit->hash == NULL) {
            ngx_free(test);
            return NGX_ERROR;
        }

        buckets = (ngx_hash_elt_t **)((u_char *) hinit->hash + sizeof(ngx_hash_wildcard_t));
    } else {
        buckets = ngx_pcalloc(hinit->pool, size/*ʵ��Ͱ����*/ * sizeof(ngx_hash_elt_t *));    //!< ����ʵ�ʹ�ϣͰ(��λ), ���� ÿ��Ͱ, Ͱ��¼����ʼ��ַ����
        if (buckets == NULL) {
            ngx_free(test);
            return NGX_ERROR;
        }
    }

    elts = ngx_palloc(hinit->pool, len + ngx_cacheline_size);                   //!< ������Ͱռ�õĿռ�������������ڴ�ռ���, ��ʼ��ַ����elts
    if (elts == NULL) {
        ngx_free(test);
        return NGX_ERROR;
    }

    elts = ngx_align_ptr(elts, ngx_cacheline_size);                             //!< elts��ʼ��ַ cacheline����

    for (i = 0; i < size; i++) {
        if (test[i] == sizeof(void *)) {
            continue;
        }

        buckets[i] = (ngx_hash_elt_t *) elts;                                   //!< ȷ��ÿ��Ͱ���׵�ַ
        elts += test[i];

    }

    for (i = 0; i < size; i++) {
        test[i] = 0;                                                            //!< ÿ��Ͱ ���г���
    }

    /** ��ÿ��<key,value>��ֵ�Ը��Ƶ����ڵ�Ͱ��
     * ���������ngx_hash_key_t *names, �������յĹ�ϣ��
     */
    for (n = 0; n < nelts; n++) {
        if (names[n].key.data == NULL) {
            continue;
        }

        key = names[n].key_hash % size;
        elt = (ngx_hash_elt_t *) ((u_char *) buckets[key] + test[key]);         //!< name[n] ��Ӧ�� element��ַ

        elt->value = names[n].value;                                            //!< value��ַ
        elt->len = (u_short) names[n].key.len;

        ngx_strlow(elt->name, names[n].key.data, names[n].key.len);

        test[key] = (u_short) (test[key] + NGX_HASH_ELT_SIZE(&names[n]));       //!< ���µ�ǰͰ�����г���
    }

    for (i = 0; i < size; i++) {                                                //!< ����ÿ��Ͱ, ��ʾÿ��Ͱ�Ľ���λ��
        if (buckets[i] == NULL) {
            continue;
        }

        elt = (ngx_hash_elt_t *) ((u_char *) buckets[i] + test[i]);

        elt->value = NULL;                                                      //!< ��ʾ ��bucket[i] ������
    }

    ngx_free(test);

    hinit->hash->buckets = buckets;         //!< ��ϣ��� Ͱ���׵�ַ
    hinit->hash->size = size;               //!< ��ϣ��� Ͱ�ĸ���

    return NGX_OK;
}

/** ��������ͨ����� "�༶��ϣ������"
 *
 * convert  "*.example.com" to "com.example.\0"     -- ǰ��ͨ���
 *      and ".example.com"  to "com.example\0"      -- ǰ������
 *      and "example.com.*" to "example.com\0"      -- ����ͨ���
 * ��ת������ַ��� ������ngx_hash_keys_arrays_t ha.[dns_wc_head | dns_wc_tail]��
 *  
 * �ο��ĵ�: 
 *  1. Nginx Դ����ʼ� - ��ϣ�� [2], http://ialloc.org/posts/2014/06/06/ngx-notes-hashtable-2/
 *  2. nginxԴ�����֮hash��ʵ��, http://www.cnblogs.com/chengxuyuancc/p/3782808.html
 *
 *
 * ����wildcard hash����,ͼ������:
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
 *  ######## dot��˼:
 *      dot==3: ��ǰwdc������һ��wdc', ��wdcָ��һ���ײ�ͨ�����server name, e.g. Example4�е�example
 *      dot==2: ��ǰwdc������һ��wdc', ��wdcָ��һ��".xxxxxxx"��server name, e.g. Example5�е�example
 *      dot==1: ��ǰwdc��  ��һ��wdc', ��wdcָ��һ���ײ�ͨ�����server name, e.g. Example5�е�mirror
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
     * 1. com.baidu.test. split(".") �ֳɷֳ�3��section��Ԫ["com", "baidu", "test"]
     * 2. �жϺ�����ڵ������� (�����������Ѿ�����ӵ����ͬ��Ԫ�����������ν���) �� ��͵�ǰ�������ĵ�һ����Ԫ��ͬ
     * 3. ����У�����Щ������ʣ�ಿ�ֶ��浽 next_names �У�ͬʱ����ѭ���в����ٴδ�����Щ������ (n = i)��
     */
    for (n = 0; n < nelts; n = i) {
#if 0
        ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0, "wc0: \"%V\"", &names[n].key);
#endif

        dot = 0;

        for (len = 0; len < names[n].key.len; len++) {  //!< 1.�ҵ�section1: ���ڵ�n��key, �ҵ���һ��Ϊ"."��λ��, lenΪ�ӿ�ʼ��"."�ĳ���
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

        dot_len = len + 1;  //!< ����"."�Ķγ���

        if (dot) {
            len++;
        }

        next_names.nelts = 0;

        if (names[n].key.len != len) {                  //!< 2. �ҵ�section2-sectionN: "."��λ���ַ���ĩβ, ��һ��"."֮������е��ַ���
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
         * 1. ����: ��ǰ׺ͨ���Ϊ��, ˵���˴���������, ��׺ͨ�����������
         * 
         * 2. ����:
         *      �жϺ�����ڵ������� (�����������Ѿ�����ӵ����ͬ��Ԫ�����������ν���) �� ��͵�ǰ�������ĵ�һ����Ԫ��ͬ��
         *      ����У�����Щ������ʣ�ಿ�ֶ��浽 next_names �У�ͬʱ����ѭ���в����ٴδ�����Щ������ (n = i)��
         * 
         * 3. server_name ԭʼ��������: 
         *      *.baidu.com         <--->   com.baidu.\0
         *      .news.baidu.com     <--->   com.baidu.news\0
         *      .sina.com.cn        <--->   cn.com.sina\0
         *      .nba.com.cn         <--->   cn.com.nba\0
         * 
         * 4. ngx_http_cmp_dns_wildcards ���������:
         *      (1) cn.com.sina\0
         *      (2) cn.com.nba\0
         *      (3) com.baidu.\0
         *      (4) com.baidu.news\0
         */
        for (i = n + 1; i < nelts; i++/*(2)-(4)���м�����(1)�ĵ�һ��section �ַ�����ͬ*/) {
            /**
             * (1) cn �� (2) cn ��ͬ, ��������, �Լ�¼����ͬcn֮��� ����ʣ���ַ���, ����ʣ���ַ�����¼��next_name��
             * (2) cn �� (3) com��ͬ, ��ֱ������for
             */
            if (ngx_strncmp(names[n].key.data, names[i].key.data, len/*��ǰnames[n].key.data ����.֮ǰ�ĳ���*/) != 0) {
                break;
            }

            /** ����
             * com.baidu\0                          ===> baidu\0                    ===> ngx_hash_wildcard_init("baidu\0")
             * com.baiduzol.\0 or com.baiduzol\0    ===> baiduzol.\0 or baiduzol\0  ===> ngx_hash_wildcard_init("baiduzol.\0")
             */
            if (!dot && names[i].key.len > len && names[i].key.data[len] != '.') {
                break;  //!< baidu\0 �� baiduzol.\0 �����Բ�ͬ, ����break�ֿ�����
            }

            next_name = ngx_array_push(&next_names);
            if (next_name == NULL) {
                return NGX_ERROR;
            }

            /**
             * ����4.(1)��4.(2), next_names �� ���� com.sina\0, com.nba\0
             */
            next_name->key.len = names[i].key.len - dot_len;
            next_name->key.data = names[i].key.data + dot_len;  //!< ��ͬ���� ����������ַ���
            next_name->key_hash = 0;
            next_name->value = names[i].value;

#if 0
            ngx_log_error(NGX_LOG_ALERT, hinit->pool->log, 0, "wc3: \"%V\"", &next_name->key);
#endif
        }

        /**
         * ֻ�����һ���ַ� next_names.nelts == 0, ���ǵݹ��ֹͣ����
         */
        if (next_names.nelts) {

            h = *hinit;
            h.hash = NULL;

            if (ngx_hash_wildcard_init(&h, (ngx_hash_key_t *) next_names.elts, next_names.nelts) != NGX_OK) {
                return NGX_ERROR;
            }

            /** ÿ��ͨ��ngx_hash_wildcard_init��̬���ɵ�h.hash, ���ַ��¼��name-value��
             * Ϊnext_names��̬�������ϣ���, ��wdc���� ��̬���ɵĵ�ַ
             * ���� ��ǰ�ֶ�( name = ngx_array_push(&curr_names);  )��value = �˵�ַ | (����λ���Ʊ��)
             */
            wdc/*ngx_hash_wildcard_t *wdc*/ = (ngx_hash_wildcard_t *) h.hash;

            /**
             * 1. ��;
             *      ��ʹ�����ngx_hash_wildcard_tͨ���ɢ�б� �洢��Ӧ��valueʱ(ngx_http_core_srv_conf_t)������ʹ�����valueָ��ָ���û�����
             * 2. ����һ
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
             * example��Ӧ�Ĺ�ϣ���� ���б���value(com.example. ��Ӧngx_http_core_srv_conf_t), �ְ��� ��һ����ϣ����(com.example.mirror.)
             * ��, ��Ҫ��value �洢 com.example. ��Ӧngx_http_core_srv_conf_t��ַ
             */
            if (names[n].key.len == len) {
                wdc->value = names[n].value;
            }

            /** ����׺��ɵ���һ��hash��ַ��Ϊ��ǰ�ֶε�value��������
             * ��δ�����һ��ʱ, name->value ָ�� ��һ����ϣ��ṹ�ĵ�ַ wdc
             *
             * dot = 2, examleָ����һ����ϣ��, ������һ��com.example���ε����, ������2��ʶ
             *	 ���Ӷ�	    ".example.com"           -> server_b                                                          
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
             *  ######## dot��˼:
             *      dot==3: ��ǰwdc������һ��wdc', ��wdcָ��һ���ײ�ͨ�����server name, e.g. ����һ�е�example
             *      dot==2: ��ǰwdc������һ��wdc', ��wdcָ��һ��".xxxxxxx"��server name, e.g. ���Ӷ��е�example
             *      dot==1: ��ǰwdc��  ��һ��wdc', ��wdcָ��һ���ײ�ͨ�����server name, e.g. ���Ӷ��е�mirror
             */
            name->value = (void *) ((uintptr_t) wdc | (dot ? 3 : 2));   //!< ��ǰ��ϣ���value ָ����һ�� �Ĺ�ϣ���ַ

        } else if (dot) {   //!< ƥ�����һ���ֶ�ʱ(example.), ����'.' ��������next_names
            name->value = (void *) ((uintptr_t) name->value | 1);   //!< ƥ�䳡��: *.example.com <--> com.example.\0
        }
    }   //!< for (n = 0; n < nelts; n = i)

    /**
     * �������һ����ֵ��<key,value>�е�value��ָ��ʵ�ʵ�value������ָ����һ����hash��ַ�������������ʵ�ֵ�һ������ĵط�
     * ����ÿ��hash��ĵ�ַ����ʵ��value�ĵ�ַ������4�ֽڶ���ģ�������Щ��ַ�ĵ�2λ����0������ͨ������λ�ı�ǿ��Ժܺõؽ���������
     */

    /**
     * �ݹ齨�� ����hash��
     * ����<��ǰ�ֶ�,value>��ֵ�Խ���hash
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

//!< ��ת����Сд, Ȼ������ϣֵ
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
        asize = NGX_HASH_LARGE_ASIZE;           //!< value �ڴ�� Ԥ�������
        ha->hsize = NGX_HASH_LARGE_HSIZE;       //!< key ��ϣͰ��
    }

    /**
     * key�洢      --- server_name��Ӧ�� ngx_str_t
     * value�洢    --- ngx_hash_key_t
     */

    if (ngx_array_init(&ha->keys, ha->temp_pool, asize, sizeof(ngx_hash_key_t))         //!< ��ȫƥ�� value�洢���� 
        != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_array_init(&ha->dns_wc_head, ha->temp_pool, asize, sizeof(ngx_hash_key_t))  //!< ǰ׺ͨ��� value�洢����
        != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_array_init(&ha->dns_wc_tail, ha->temp_pool, asize, sizeof(ngx_hash_key_t))  //!< ��׺ͨ��� value�洢����
        != NGX_OK) {
        return NGX_ERROR;
    }

    ha->keys_hash = ngx_pcalloc(ha->temp_pool, sizeof(ngx_array_t) * ha->hsize);        //!< ��ȫƥ�� key��ϣ��, Ͱ����Ϊha->hsize
    if (ha->keys_hash == NULL) {
        return NGX_ERROR;
    }

    ha->dns_wc_head_hash = ngx_pcalloc(ha->temp_pool, sizeof(ngx_array_t) * ha->hsize); //!< ǰ׺ͨ��� key��ϣ��, Ͱ����Ϊha->hsize
    if (ha->dns_wc_head_hash == NULL) {
        return NGX_ERROR;
    }

    ha->dns_wc_tail_hash = ngx_pcalloc(ha->temp_pool, sizeof(ngx_array_t) * ha->hsize); //!< ��׺ͨ��� key��ϣ��, Ͱ����Ϊha->hsize
    if (ha->dns_wc_tail_hash == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_hash_add_key(ngx_hash_keys_arrays_t *ha, ngx_str_t *key, void *value,
    ngx_uint_t flags/*��� �Ƿ����ͨ���*/)
{
    size_t           len;
    u_char          *p;
    ngx_str_t       *name;
    ngx_uint_t       i, k, n, skip, last;
    ngx_array_t     *keys, *hwc;
    ngx_hash_key_t  *hk;
                        //!< skipΪȥ��ͨ������ ��һ����Ч�ַ���λ��, ��0��ʼ
    last = key->len;    //!< lastΪ���һ����Ч�ַ� ��Ӧ����Ч�ַ�������, ��1��ʼ

    /**
     * ���ڴ��� ͨ�������, �������if ��sanity check
     */
    if (flags & NGX_HASH_WILDCARD_KEY) {

        /*
         * supported wildcards:
         *     "*.example.com", ".example.com", and "www.example.*"
         */

        n = 0;

        /**
         * sanity check: 1)*ʹ������; 2)ֻ�ܴ���һ��.
         */
        for (i = 0; i < key->len; i++) {

            /** *ʹ������: 1)ͨ���ֻ���ڿ�ͷor��β; 2)������.����: *.xxx or xxx.*
             * A wildcard name may contain an asterisk only on the name's start or end, and only on a dot border.
             * An asterisk can match several name parts.
             */
            if (key->data[i] == '*') {
                if (++n > 1) {
                    return NGX_DECLINED;
                }
            }

            /**
             * ֻ�ܴ���һ��.
             */
            if (key->data[i] == '.' && key->data[i + 1] == '.') {
                return NGX_DECLINED;
            }
        }

        /**
         * .example.com, skip==1
         */
        if (key->len > 1 && key->data[0] == '.') {
            skip = 1;           //!< skipΪ��һ����Ч�ַ���λ��
            goto wildcard;      //!< ͨ�������
        }

        if (key->len > 2) {

            /**
             * *.example.com, skip==2
             */
            if (key->data[0] == '*' && key->data[1] == '.') {
                skip = 2;       //!< skipΪ��һ����Ч�ַ���λ��
                goto wildcard;  //!< ͨ�������
            }

            /**
             * example.com.*, skip==-2
             */
            if (key->data[i - 2] == '.' && key->data[i - 1] == '*') {
                skip = 0;       //!< skipΪ��һ����Ч�ַ���λ��
                last -= 2;      //!< lastΪ���һ����Ч�ַ� ��Ӧ����Ч�ַ�������
                goto wildcard;  //!< ͨ�������
            }
        }

        if (n) {    //!< ������ *.xxx or xxx.* ͨ�������(ͨ����������м�λ��), ����Ϊ��Ч����
            return NGX_DECLINED;
        }
    }

    /* exact hash */
    /* ��ͨ��� hash */
    k = 0;

    for (i = 0; i < last/*���һ����Ч�ַ���λ��*/; i++) {
        if (!(flags & NGX_HASH_READONLY_KEY)) {
            key->data[i] = ngx_tolower(key->data[i]);   //!< ת��Сд
        }
        k = ngx_hash(k, key->data[i]);                  //!< �����ַ����� hashֵ
    }

    k %= ha->hsize; //!< key��Ӧ�� hash buckets ��λ

    /* check conflicts in exact hash */
    //!< ����Ƿ��г�ͻ
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

    *name = *key;   //!< key�洢 --- ngx_str_t ��ֵ����

    /**
     * value �洢 --- ngx_hash_key_t
     */
    hk = ngx_array_push(&ha->keys);
    if (hk == NULL) {
        return NGX_ERROR;
    }

    /**
     * ��ʼ��ngx_hash_key_t, ����䲻����ͨ����Ĺ�ϣ��
     */
    hk->key = *key;                                 //< ngx_str_t
    hk->key_hash = ngx_hash_key(key->data, last);   //!< hash value
    hk->value = value;                              //!< value ��ַ��ֵ

    return NGX_OK;


wildcard:

    /* wildcard hash */

    k = ngx_hash_strlow(&key->data[skip], &key->data[skip], last - skip);   //!< ȥ��ͨ���, ����hashֵ

    k %= ha->hsize;     //!< hash bucket num

    if (skip == 1) {    //!< .example.com, skip==1

        /* check conflicts in exact hash for ".example.com" */
        //!< ��� key �Ƿ��г�ͻ
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

        name = ngx_array_push(&ha->keys_hash[k]);           //!< key �洢
        if (name == NULL) {
            return NGX_ERROR;
        }

        name->len = last - 1;
        name->data = ngx_pnalloc(ha->temp_pool, name->len);
        if (name->data == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(name->data, &key->data[1], name->len);   //!< ʡ��.
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
         * ��ת������ַ��� ������p[]��
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

        if (len) {  //!< *.example.com����, forѭ���� len==1
            ngx_memcpy(&p[n], &key->data[1], len);  //!< ����.
            n += len;
        }

        p[n] = '\0';    //!< ������

        hwc = &ha->dns_wc_head;             //!< wildcard header value�洢
        keys = &ha->dns_wc_head_hash[k];    //!< wildcard header key�洢

    } else {    //!< example.com.*(����ͨ���), skip==-2

        /* convert "www.example.*" to "www.example\0" */

        last++; //!< ֮ǰ, last = key->len - 2; ��ʱ++,Ϊ������һ��\0λ��

        p = ngx_pnalloc(ha->temp_pool, last);
        if (p == NULL) {
            return NGX_ERROR;
        }

        ngx_cpystrn(p, key->data, last);    //!< ngx_cpystrn������p�Ľ�����'\0'

        hwc = &ha->dns_wc_tail;             //!< wildcard tail value�洢
        keys = &ha->dns_wc_tail_hash[k];    //!< wildcard tail key�洢
    }

    hk = ngx_array_push(hwc);               //!< wildcard header/tail value�洢
    if (hk == NULL) {
        return NGX_ERROR;
    }

    hk->key.len = last - 1;                 //!< ȥ��'\0'
    hk->key.data = p;                       //!< �� ������ͨ���, ����
    hk->key_hash = 0;
    hk->value = value;                      //!< value�洢���ǵ�ַ

    /* check conflicts in wildcard hash */
    //!< wildcard ��key�� ����Ƿ��г�ͻ
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

    name = ngx_array_push(keys);        //!< wildcard header/tail key�洢
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
